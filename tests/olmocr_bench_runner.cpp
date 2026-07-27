#include "export/structured_text_document_exporter.h"
#include "pipeline/document_engine.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path pdf_root;
    std::filesystem::path output_root;
    std::filesystem::path work_directory;
    std::filesystem::path report;
    std::string category;
    int dpi = 200;
    std::size_t limit = 0;
    bool resume = false;
};

bool parseSize(const std::string& value, std::size_t& output) {
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(value, &consumed);
        if (consumed != value.size()) {
            return false;
        }
        output = static_cast<std::size_t>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parsePositiveInt(const std::string& value, int& output) {
    std::size_t parsed = 0;
    if (!parseSize(value, parsed) || parsed == 0 ||
        parsed > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    output = static_cast<int>(parsed);
    return true;
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--resume") {
            options.resume = true;
            continue;
        }
        if (index + 1 >= argc) {
            return false;
        }
        const std::string value = argv[++index];
        if (argument == "--pdf-root") {
            options.pdf_root = value;
        } else if (argument == "--output-root") {
            options.output_root = value;
        } else if (argument == "--work-dir") {
            options.work_directory = value;
        } else if (argument == "--report") {
            options.report = value;
        } else if (argument == "--category") {
            options.category = value;
        } else if (argument == "--dpi") {
            if (!parsePositiveInt(value, options.dpi)) {
                return false;
            }
        } else if (argument == "--limit") {
            if (!parseSize(value, options.limit)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return !options.pdf_root.empty() && !options.output_root.empty() && !options.work_directory.empty() &&
           !options.report.empty();
}

bool inCategory(const std::filesystem::path& relative, const std::string& category) {
    return category.empty() || (relative.begin() != relative.end() && relative.begin()->string() == category);
}

std::vector<std::filesystem::path> inputPdfs(const Options& options) {
    std::vector<std::filesystem::path> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(options.pdf_root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") {
            continue;
        }
        const std::filesystem::path relative = entry.path().lexically_relative(options.pdf_root);
        if (inCategory(relative, options.category)) {
            result.push_back(relative);
        }
    }
    std::sort(result.begin(), result.end());
    if (options.limit > 0 && result.size() > options.limit) {
        result.resize(options.limit);
    }
    return result;
}

std::filesystem::path candidatePath(const Options& options, const std::filesystem::path& relative_pdf) {
    const std::string filename = relative_pdf.stem().string() + "_pg1_repeat1.md";
    return options.output_root / relative_pdf.parent_path() / filename;
}

bool writeEmptyCandidate(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    const std::ofstream output(path, std::ios::trunc);
    return static_cast<bool>(output);
}

long long elapsedMilliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        std::cerr << "Usage: " << argv[0]
                  << " --pdf-root bench_data/pdfs --output-root bench_data/candidate --work-dir work"
                     " --report report.json [--category name] [--dpi value] [--limit count] [--resume]\n";
        return 2;
    }
    if (!std::filesystem::is_directory(options.pdf_root)) {
        std::cerr << "PDF root is not a directory: " << options.pdf_root << '\n';
        return 2;
    }

    const std::vector<std::filesystem::path> pdfs = inputPdfs(options);
    if (pdfs.empty()) {
        std::cerr << "No PDFs matched the requested benchmark scope\n";
        return 2;
    }

    doc_parser::pipeline::EngineConfig config = doc_parser::pipeline::defaultEngineConfig();
    config.backends.document = "pdf";
    config.backends.ocr = "auto";
    config.backends.layout = "auto";
    config.backends.table = "auto";
    doc_parser::pipeline::DocumentEngine engine(config);
    if (!engine.isReady()) {
        std::cerr << "Document engine initialization failed: " << engine.initializationStatus().message() << '\n';
        return 1;
    }

    nlohmann::json report = {
        {"version", 1},
        {"benchmark", "olmOCR-bench"},
        {"candidate", "DocumentIntelligenceEngine"},
        {"config",
         {
             {"pdf_root", options.pdf_root.generic_string()},
             {"category", options.category.empty() ? "all" : options.category},
             {"dpi", options.dpi},
             {"limit", options.limit},
             {"resume", options.resume},
             {"backends",
              {
                  {"document", config.backends.document},
                  {"ocr", config.backends.ocr},
                  {"layout", config.backends.layout},
                  {"table", config.backends.table},
              }},
         }},
        {"documents", nlohmann::json::array()},
    };

    const auto run_start = std::chrono::steady_clock::now();
    std::size_t succeeded = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;
    const std::filesystem::path current_work = options.work_directory / "current";
    const doc_parser::exporter::MarkdownDocumentExporter exporter;

    for (std::size_t index = 0; index < pdfs.size(); ++index) {
        const std::filesystem::path& relative_pdf = pdfs[index];
        const std::filesystem::path input = options.pdf_root / relative_pdf;
        const std::filesystem::path output = candidatePath(options, relative_pdf);
        nlohmann::json item = {
            {"pdf", relative_pdf.generic_string()},
            {"output", output.lexically_relative(options.output_root).generic_string()},
        };

        if (options.resume && std::filesystem::is_regular_file(output)) {
            item["status"] = "skipped";
            report["documents"].push_back(std::move(item));
            ++skipped;
            std::cout << '[' << index + 1 << '/' << pdfs.size() << "] skipped " << relative_pdf << '\n';
            continue;
        }

        std::filesystem::remove_all(current_work);
        std::filesystem::create_directories(current_work);
        std::filesystem::create_directories(output.parent_path());
        doc_parser::pipeline::PipelineRunOptions run_options;
        run_options.input_path = input;
        run_options.output_directory = current_work;
        run_options.render.dpi = options.dpi;

        const auto document_start = std::chrono::steady_clock::now();
        const doc_parser::pipeline::ParseResult result = engine.parse(run_options);
        doc_parser::common::Status export_status = doc_parser::common::Status::ok();
        if (result.ok()) {
            export_status = exporter.write({false, output, &result.document, &result.artifacts});
        }
        item["elapsed_ms"] = elapsedMilliseconds(document_start);
        item["blocks"] = result.document.blocks.size();

        if (result.ok() && export_status.okStatus()) {
            item["status"] = "succeeded";
            ++succeeded;
        } else {
            item["status"] = "failed";
            item["stage"] = result.ok() ? export_status.stage() : result.status.stage();
            item["code"] = result.ok() ? export_status.code() : result.status.code();
            item["message"] = result.ok() ? export_status.message() : result.status.message();
            if (!writeEmptyCandidate(output)) {
                std::cerr << "Failed to represent benchmark failure at " << output << '\n';
                return 1;
            }
            ++failed;
        }
        report["documents"].push_back(std::move(item));
        std::cout << '[' << index + 1 << '/' << pdfs.size() << "] " << relative_pdf
                  << " blocks=" << result.document.blocks.size()
                  << " elapsed_ms=" << elapsedMilliseconds(document_start) << '\n';
    }

    std::filesystem::remove_all(current_work);
    report["summary"] = {
        {"documents", pdfs.size()},
        {"succeeded", succeeded},
        {"failed", failed},
        {"skipped", skipped},
        {"elapsed_ms", elapsedMilliseconds(run_start)},
    };
    if (!options.report.parent_path().empty()) {
        std::filesystem::create_directories(options.report.parent_path());
    }
    std::ofstream report_output(options.report);
    report_output << report.dump(2) << '\n';
    if (!report_output) {
        std::cerr << "Failed to write report: " << options.report << '\n';
        return 1;
    }
    std::cout << "completed documents=" << pdfs.size() << " succeeded=" << succeeded << " failed=" << failed
              << " skipped=" << skipped << " elapsed_ms=" << elapsedMilliseconds(run_start) << '\n';
    return 0;
}
