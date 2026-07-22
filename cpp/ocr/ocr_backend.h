#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"

#include <string>
#include <vector>

namespace doc_parser::ocr {

struct OcrRequest {
    const document::PageArtifact& page;
    int dpi = 200;
};

struct OcrRegion {
    document::BBox bbox;
    double detection_confidence = 0.0;
    std::string text;
    double recognition_confidence = 0.0;
};

struct OcrResult {
    document::PageText page_text;
    std::vector<OcrRegion> regions;
};

struct OcrDetectionResult {
    std::vector<OcrRegion> regions;
};

struct OcrRegionRequest {
    const document::PageArtifact& page;
    int dpi = 200;
    std::vector<document::BBox> regions;
};

struct OcrRegionRecognitionResult {
    std::vector<OcrRegion> regions;
};

class IOcrBackend {
public:
    virtual ~IOcrBackend() = default;

    virtual bool recognize(const OcrRequest& request, OcrResult& result) const = 0;
    virtual bool detect(const OcrRequest&, OcrDetectionResult&) const { return false; }
    virtual bool recognizeRegions(const OcrRegionRequest&, OcrRegionRecognitionResult&) const { return false; }
    virtual std::string unavailableReason() const { return {}; }
};

class NoopOcrBackend final : public IOcrBackend {
public:
    bool recognize(const OcrRequest& request, OcrResult& result) const override;
};

class UnavailableOcrBackend final : public IOcrBackend {
public:
    explicit UnavailableOcrBackend(std::string reason);

    bool recognize(const OcrRequest& request, OcrResult& result) const override;
    std::string unavailableReason() const override;

private:
    std::string reason_;
};

} // namespace doc_parser::ocr
