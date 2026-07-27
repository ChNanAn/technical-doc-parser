#include "pipeline/backend_registry.h"
#include "pipeline/engine_config.h"
#include "redis_client.h"
#include "worker_document_processor.h"
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

struct WorkerRunOptions {
    doc_parser::pipeline::DocumentParseOptions parse;
    doc_parser::pipeline::BackendOptions backends;
};

WorkerRunOptions optionsFromJob(const nlohmann::json& job,
                                const doc_parser::pipeline::BackendOptions& default_backends) {
    const nlohmann::json& pipeline = job.at("pipeline");
    const nlohmann::json& backends = pipeline.at("backends");
    WorkerRunOptions options;
    options.parse.input_path = localFilePath(job.at("input").at("uri").get<std::string>());
    options.parse.output_directory = job.at("output_directory").get<std::string>();
    options.parse.render.dpi = pipeline.at("dpi").get<int>();
    options.parse.debug = pipeline.at("debug").get<bool>();
    doc_parser::pipeline::BackendOptions requested_backends;
    requested_backends.document = backends.at("document").get<std::string>();
    requested_backends.ocr = backends.at("ocr").get<std::string>();
    requested_backends.layout = backends.at("layout").get<std::string>();
    requested_backends.table = backends.at("table").get<std::string>();
    requested_backends.registry_config = backends.value("registry_config", "");
    options.backends = doc_parser::platform::effectiveBackendOptions(default_backends, requested_backends);
    options.parse.timeout_seconds = job.at("limits").at("timeout_seconds").get<int>();
    options.parse.maximum_pages = job.at("limits").at("maximum_pages").get<int>();
    return options;
}

std::string availableCapabilities(const doc_parser::pipeline::BackendRegistry& registry) {
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

nlohmann::json engineConfigJson(const doc_parser::pipeline::EngineConfig& config) {
    return {
        {"backends",
         {
             {"document", config.backends.document},
             {"ocr", config.backends.ocr},
             {"layout", config.backends.layout},
             {"table", config.backends.table},
             {"registry_config", config.backends.registry_config.string()},
         }},
        {"tesseract", {{"executable", config.tesseract.executable}, {"language", config.tesseract.language}}},
        {"paddle_ocr",
         {
             {"detection_model", config.paddle_ocr.detection_model.string()},
             {"recognition_model", config.paddle_ocr.recognition_model.string()},
             {"character_dict", config.paddle_ocr.character_dict.string()},
             {"profile", config.paddle_ocr.profile.name},
             {"recognition_batch_size", config.paddle_ocr.recognition_batch_size},
             {"recognition_max_width", config.paddle_ocr.recognition_max_width},
             {"detection_limit_side", config.paddle_ocr.detection_limit_side},
         }},
        {"doclaynet",
         {
             {"model", config.doclaynet.model_path.string()},
             {"confidence_threshold", config.doclaynet.confidence_threshold},
         }},
        {"paddle_layout",
         {
             {"model", config.paddle_layout.model_path.string()},
             {"confidence_threshold", config.paddle_layout.confidence_threshold},
         }},
        {"table_transformer",
         {
             {"detection_model", config.table_transformer.detection_model_path.string()},
             {"structure_model", config.table_transformer.structure_model_path.string()},
             {"detection_confidence_threshold", config.table_transformer.detection_confidence_threshold},
             {"structure_confidence_threshold", config.table_transformer.structure_confidence_threshold},
             {"crop_padding", config.table_transformer.crop_padding},
         }},
    };
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

int main(int argc, char** argv) {
    std::signal(SIGINT, stopWorker);
    std::signal(SIGTERM, stopWorker);

    if (argc > 2 || (argc == 2 && std::string(argv[1]) != "--print-engine-config")) {
        std::cerr << "Usage: " << argv[0] << " [--print-engine-config]\n";
        return 2;
    }

    const doc_parser::pipeline::EngineConfig engine_config = doc_parser::pipeline::engineConfigFromEnvironment();
    if (argc == 2) {
        std::cout << engineConfigJson(engine_config).dump(2) << '\n';
        return 0;
    }

    const std::string redis_host = environment("REDIS_HOST", "127.0.0.1");
    const int redis_port = environmentInt("REDIS_PORT", 6379);
    const std::string job_stream = environment("JOB_STREAM", "document-jobs");
    const std::string consumer_group = environment("JOB_CONSUMER_GROUP", "document-workers");
    const std::string worker_id = environment("WORKER_ID", "worker-1");
    const std::filesystem::path runtime_root = environment("WORKER_RUNTIME_ROOT", "");
    const int engine_cache_size = environmentInt("WORKER_ENGINE_CACHE_SIZE", 2);
    const int run_event_stream_maximum_length = environmentInt("RUN_EVENT_STREAM_MAX_LENGTH", 2'000);
    const int platform_event_stream_maximum_length = environmentInt("PLATFORM_EVENT_STREAM_MAX_LENGTH", 100'000);
    if (engine_cache_size <= 0 || run_event_stream_maximum_length <= 0 || platform_event_stream_maximum_length <= 0) {
        std::cerr << "WORKER_ENGINE_CACHE_SIZE, RUN_EVENT_STREAM_MAX_LENGTH, and PLATFORM_EVENT_STREAM_MAX_LENGTH must "
                     "be positive\n";
        return 2;
    }

    try {
        const doc_parser::pipeline::BackendRegistry backend_registry =
            doc_parser::pipeline::createDefaultBackendRegistry(engine_config);
        doc_parser::platform::RedisClient redis(redis_host, redis_port);
        redis.ensureConsumerGroup(job_stream, consumer_group);
        const std::string worker_key = "worker:" + worker_id;
        const std::string capabilities = availableCapabilities(backend_registry);
        std::cout << "worker engine config: " << engineConfigJson(engine_config).dump() << '\n';
        WorkerHeartbeat heartbeat(redis_host, redis_port, worker_key, capabilities);
        doc_parser::platform::WorkerDocumentProcessor processor(
            engine_config, backend_registry, static_cast<std::size_t>(engine_cache_size));

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
                doc_parser::platform::WorkerStageObserver observer(
                    redis,
                    job_id,
                    run_id,
                    attempt_id,
                    run_directory,
                    static_cast<std::size_t>(run_event_stream_maximum_length),
                    static_cast<std::size_t>(platform_event_stream_maximum_length));
                heartbeat.setRunning(run_id);
                observer.publishJobEvent("job_started");
                try {
                    const WorkerRunOptions options = optionsFromJob(job, engine_config.backends);
                    const doc_parser::common::Status status =
                        processor.process(options.parse, options.backends, observer);
                    observer.publishJobEvent(status.okStatus() ? "job_succeeded" : "job_failed", status.message());
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
