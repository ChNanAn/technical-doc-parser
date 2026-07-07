#pragma once

#include "document/page_artifact.h"
#include "backend/pdf/pdf_document.h"

#include <filesystem>
#include <vector>

namespace doc_parser::pdf {

struct RenderRequest {
    int dpi = 200;
    std::filesystem::path output_root;
    std::filesystem::path pages_dir;
};

// 渲染操作。无状态，操作通过 const PdfDocument& 接收 PDF 源。
class RenderService {
public:
    RenderService() = default;

    bool renderPages(const PdfDocument& source,
                     const RenderRequest& request,
                     std::vector<document::PageArtifact>& pages) const;
};

} // namespace doc_parser::pdf
