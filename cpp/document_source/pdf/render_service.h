#pragma once

#include "document/page_artifact.h"
#include "document_source/document_source_interfaces.h"
#include "document_source/pdf/pdf_document.h"

#include <filesystem>
#include <vector>

namespace doc_parser::pdf {

using RenderRequest = document_source::RenderRequest;

// 渲染操作。无状态，操作通过 const PdfDocument& 接收 PDF 源。
class RenderService {
public:
    RenderService() = default;

    bool renderPages(const PdfDocument& source,
                     const RenderRequest& request,
                     std::vector<document::PageArtifact>& pages) const;
    bool renderPage(const PdfDocument& source,
                    const RenderRequest& request,
                    int page_index,
                    document::PageArtifact& page) const;
};

} // namespace doc_parser::pdf
