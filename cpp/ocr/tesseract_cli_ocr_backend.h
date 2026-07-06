#pragma once

#include "ocr/ocr_backend.h"

#include <string>

namespace doc_parser::ocr {

class TesseractCliOcrBackend final : public IOcrBackend {
public:
    TesseractCliOcrBackend();
    explicit TesseractCliOcrBackend(std::string language);
    TesseractCliOcrBackend(std::string executable, std::string language);

    bool recognize(const OcrRequest& request, OcrResult& result) const override;

    bool isAvailable() const;

private:
    std::string executable_;
    std::string language_;
};

} // namespace doc_parser::ocr
