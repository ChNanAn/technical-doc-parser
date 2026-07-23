#include "app/cli_options.h"
#include "pipeline/backend_registry.h"
#include "pipeline/document_pipeline.h"
#include "redis_client.h"
#include "worker_stage_observer.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::atomic<bool> running{true};

void stopWorker(int) { running = false; }

std::string environment(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value == nullptr || std::string(value).empty() ? fallback : value;
}

int environmentInt(const char* name, int fallback) {
    try {
        return std::stoi(environment(name, std::to_string(fallback)));
    } catch (const std::exception&) {
        return fallback;
    }
}

std::filesystem::path localFilePath(const std::string& uri) {
    constexpr const char* prefix = "file://";
    if (uri.rfind(prefix, 0) != 0) {
        throw std::runtime_error("worker v1 only accepts file:// input URIs");
    }
    return uri.substr(std::char_traits<char>::length(prefix));
}

nlohmann::json loadJob(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to read Job document: " + path.string());
    }
    nlohmann::json job = nlohmann::json::parse(input);
    if (job.value("schema_version", 0) != 1) {
        throw std::runtime_error("unsupported Job schema version");
    }
    return job;
}

bool isInside(const std::filesystem::path& path, const std::filesystem::path& root) {
    if (root.empty()) {
        return true;
    }
    const std::filesystem::path resolved_path = std::filesystem::weakly_canonical(path);
    const std::filesystem::path resolved_root = std::filesystem::weakly_canonical(root);
    return resolved_path == resolved_root || resolved_path.string().rfind(resolved_root.string() + '/', 0) == 0;
}

void validateJob(const nlohmann::json& job, const std::filesystem::path& runtime_root) {
    const std::filesystem::path input = localFilePath(job.at("input").at("uri").get<std::string>());
    const std::filesystem::path output = job.at("output_directory").get<std::string>();
    if (!std::filesystem::is_regular_file(input)) {
        throw std::runtime_error("Job input is not a regular file: " + input.string());
    }
    if (!isInside(input, runtime_root) || !isInside(output, runtime_root)) {
        throw std::runtime_error("Job input and output must remain inside WORKER_RUNTIME_ROOT");
    }
    const auto expected_size = job.at("input").value("size_bytes", std::uintmax_t{0});
    if (expected_size > 0 && std::filesystem::file_size(input) != expected_size) {
        throw std::runtime_error("Job input size does not match its protocol metadata");
    }
    std::ifstream stream(input, std::ios::binary);
    char header[5]{};
    stream.read(header, 5);
    if (stream.gcount() != 5 || std::string(header, 5) != "%PDF-") {
        throw std::runtime_error("Job input does not have a PDF file signature");
    }
    const nlohmann::json& limits = job.at("limits");
    const int timeout_seconds = limits.at("timeout_seconds").get<int>();
    const int maximum_pages = limits.at("maximum_pages").get<int>();
    if (timeout_seconds < 1 || timeout_seconds > 86400 || maximum_pages < 1 || maximum_pages > 10000) {
        throw std::runtime_error("Job limits are outside the supported protocol range");
    }
}

doc_parser::app::CliOptions optionsFromJob(const nlohmann::json& job) {
    const nlohmann::json& pipeline = job.at("pipeline");
    const nlohmann::json& backends = pipeline.at("backends");
    doc_parser::app::CliOptions options;
    options.input_pdf = localFilePath(job.at("input").at("uri").get<std::string>()).string();
    options.output_dir = job.at("output_directory").get<std::string>();
    options.dpi = pipeline.at("dpi").get<int>();
    options.debug = pipeline.at("debug").get<bool>();
    options.document_backend = backends.at("document").get<std::string>();
    options.ocr_backend = backends.at("ocr").get<std::string>();
    options.layout_backend = backends.at("layout").get<std::string>();
    options.table_backend = backends.at("table").get<std::string>();
    options.backend_config = backends.value("registry_config", "");
    options.timeout_seconds = job.at("limits").at("timeout_seconds").get<int>();
    options.maximum_pages = job.at("limits").at("maximum_pages").get<int>();
    return options;
}

std::string availableCapabilities() {
    const doc_parser::pipeline::BackendRegistry registry = doc_parser::pipeline::createDefaultBackendRegistry();
    nlohmann::json capabilities = {
        {"document", nlohmann::json::array({"auto"})},
        {"ocr", nlohmann::json::array({"auto"})},
        {"layout", nlohmann::json::array({"auto"})},
        {"table", nlohmann::json::array({"auto"})},
    };
    for (const std::string& name : registry.documentNames()) {
        if (registry.createDocument(name).status == doc_parser::pipeline::BackendCreationStatus::Created) {
            capabilities["document"].push_back(name);
        }
    }
    for (const std::string& name : registry.ocrNames()) {
        if (registry.createOcr(name).status == doc_parser::pipeline::BackendCreationStatus::Created) {
            capabilities["ocr"].push_back(name);
        }
    }
    for (const std::string& name : registry.layoutNames()) {
        if (registry.createLayout(name).status == doc_parser::pipeline::BackendCreationStatus::Created) {
            capabilities["layout"].push_back(name);
        }
    }
    for (const std::string& name : registry.tableNames()) {
        if (registry.createTable(name).status == doc_parser::pipeline::BackendCreationStatus::Created) {
            capabilities["table"].push_back(name);
        }
    }
    return capabilities.dump();
}

class WorkerHeartbeat {
public:
    WorkerHeartbeat(std::string redis_host, int redis_port, std::string worker_key, std::string capabilities)
        : redis_host_(std::move(redis_host)), redis_port_(redis_port), worker_key_(std::move(worker_key)),
          capabilities_(std::move(capabilities)), thread_(&WorkerHeartbeat::run, this) {}

    ~WorkerHeartbeat() {
        stop_ = true;
        condition_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    WorkerHeartbeat(const WorkerHeartbeat&) = delete;
    WorkerHeartbeat& operator=(const WorkerHeartbeat&) = delete;

    void setIdle() { setState("idle", ""); }
    void setRunning(const std::string& run_id) { setState("running", run_id); }

private:
    void setState(std::string status, std::string run_id) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_ = std::move(status);
            run_id_ = std::move(run_id);
        }
        condition_.notify_all();
    }

    void run() {
        std::unique_ptr<doc_parser::platform::RedisClient> redis;
        while (!stop_) {
            std::string status;
            std::string run_id;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                status = status_;
                run_id = run_id_;
            }
            try {
                if (redis == nullptr) {
                    redis = std::make_unique<doc_parser::platform::RedisClient>(redis_host_, redis_port_);
                }
                redis->setHash(worker_key_, {{"status", status}, {"run_id", run_id}, {"capabilities", capabilities_}});
                redis->expire(worker_key_, 30);
            } catch (const std::exception& error) {
                std::cerr << "worker heartbeat failed: " << error.what() << '\n';
                redis.reset();
            }
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait_for(lock, std::chrono::seconds(10));
        }
    }

    std::string redis_host_;
    int redis_port_;
    std::string worker_key_;
    std::string capabilities_;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::condition_variable condition_;
    std::string status_ = "idle";
    std::string run_id_;
    std::thread thread_;
};

} // namespace

int main() {
    std::signal(SIGINT, stopWorker);
    std::signal(SIGTERM, stopWorker);

    const std::string redis_host = environment("REDIS_HOST", "127.0.0.1");
    const int redis_port = environmentInt("REDIS_PORT", 6379);
    const std::string job_stream = environment("JOB_STREAM", "document-jobs");
    const std::string consumer_group = environment("JOB_CONSUMER_GROUP", "document-workers");
    const std::string worker_id = environment("WORKER_ID", "worker-1");
    const std::filesystem::path runtime_root = environment("WORKER_RUNTIME_ROOT", "");

    try {
        doc_parser::platform::RedisClient redis(redis_host, redis_port);
        redis.ensureConsumerGroup(job_stream, consumer_group);
        const std::string worker_key = "worker:" + worker_id;
        const std::string capabilities = availableCapabilities();
        WorkerHeartbeat heartbeat(redis_host, redis_port, worker_key, capabilities);

        while (running) {
            heartbeat.setIdle();
            const auto message = redis.readGroup(job_stream, consumer_group, worker_id, 5000);
            if (!message.has_value()) {
                continue;
            }
            const auto job_path = message->fields.find("job_path");
            if (job_path == message->fields.end()) {
                std::cerr << "queue message " << message->id << " has no job_path\n";
                redis.acknowledge(job_stream, consumer_group, message->id);
                continue;
            }

            try {
                const nlohmann::json job = loadJob(job_path->second);
                validateJob(job, runtime_root);
                const std::string job_id = job.at("job_id").get<std::string>();
                const std::string run_id = job.at("run_id").get<std::string>();
                const std::string attempt_id = job.at("attempt_id").get<std::string>();
                const std::filesystem::path run_directory =
                    std::filesystem::path(job.at("output_directory").get<std::string>()).parent_path();
                doc_parser::platform::WorkerStageObserver observer(redis, job_id, run_id, attempt_id, run_directory);
                heartbeat.setRunning(run_id);
                observer.publishJobEvent("job_started");
                const doc_parser::pipeline::DocumentPipeline pipeline;
                try {
                    const bool success = pipeline.run(optionsFromJob(job), observer);
                    observer.publishJobEvent(success ? "job_succeeded" : "job_failed", "");
                } catch (const std::exception& error) {
                    observer.publishJobEvent("job_failed", error.what());
                }
                redis.acknowledge(job_stream, consumer_group, message->id);
            } catch (const std::exception& error) {
                std::cerr << "job " << message->id << " failed: " << error.what() << '\n';
                const auto run_id = message->fields.find("run_id");
                if (run_id != message->fields.end()) {
                    redis.setHash("run:" + run_id->second, {{"status", "failed"}, {"error", error.what()}});
                }
                redis.acknowledge(job_stream, consumer_group, message->id);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "worker fatal error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
