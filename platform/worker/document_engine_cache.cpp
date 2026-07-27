#include "document_engine_cache.h"

#include "pipeline/document_engine_internal.h"

#include <filesystem>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace doc_parser::platform {
namespace {

std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }

    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
        absolute = path;
    }
    std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

pipeline::BackendOptions normalizedBackends(pipeline::BackendOptions backends) {
    backends.registry_config = normalizedPath(backends.registry_config);
    return backends;
}

bool sameBackends(const pipeline::BackendOptions& left, const pipeline::BackendOptions& right) {
    return left.document == right.document && left.ocr == right.ocr && left.layout == right.layout &&
           left.table == right.table && left.registry_config == right.registry_config;
}

std::string backendSummary(const pipeline::BackendOptions& backends) {
    return "document=" + backends.document + ", ocr=" + backends.ocr + ", layout=" + backends.layout +
           ", table=" + backends.table +
           ", registry=" + (backends.registry_config.empty() ? "builtin" : backends.registry_config.string());
}

} // namespace

pipeline::BackendOptions effectiveBackendOptions(const pipeline::BackendOptions& defaults,
                                                 const pipeline::BackendOptions& requested) {
    pipeline::BackendOptions effective = requested;
    if (effective.registry_config.empty()) {
        effective.registry_config = defaults.registry_config;
    }
    return effective;
}

DocumentEngineCache::DocumentEngineCache(pipeline::EngineConfig base_config,
                                         const pipeline::BackendRegistry& registry,
                                         std::size_t capacity)
    : base_config_(std::move(base_config)), registry_(registry), capacity_(capacity) {
    if (capacity_ == 0) {
        throw std::invalid_argument("document engine cache capacity must be positive");
    }
}

DocumentEngineLookup DocumentEngineCache::get(const pipeline::BackendOptions& backends) {
    pipeline::BackendOptions normalized = normalizedBackends(backends);
    for (auto entry = entries_.begin(); entry != entries_.end(); ++entry) {
        if (!sameBackends(entry->backends, normalized)) {
            continue;
        }
        entries_.splice(entries_.begin(), entries_, entry);
        spdlog::info("worker engine cache hit: {}", backendSummary(normalized));
        return {entries_.front().engine.get(), common::Status::ok(), true};
    }

    pipeline::EngineConfig config = base_config_;
    config.backends = normalized;
    auto engine = pipeline::DocumentEngineInternalAccess::createUnique(std::move(config), registry_);
    if (!engine->isReady()) {
        const common::Status status = engine->initializationStatus();
        spdlog::warn("worker engine cache rejected: {} code={} reason={}",
                     backendSummary(normalized),
                     status.code(),
                     status.message());
        return {nullptr, status, false};
    }

    if (entries_.size() == capacity_) {
        spdlog::info("worker engine cache eviction: {}", backendSummary(entries_.back().backends));
        entries_.pop_back();
    }

    pipeline::DocumentEngine* value = engine.get();
    entries_.push_front({std::move(normalized), std::move(engine)});
    spdlog::info("worker engine cache miss: {}", backendSummary(entries_.front().backends));
    return {value, common::Status::ok(), false};
}

std::size_t DocumentEngineCache::size() const { return entries_.size(); }

std::size_t DocumentEngineCache::capacity() const { return capacity_; }

} // namespace doc_parser::platform
