#pragma once

#include "document/page_artifact.h"
#include "document_source/document_source_interfaces.h"

#include <filesystem>
#include <vector>

namespace doc_parser::pdf {

class PdfReader;
using RenderRequest = document_source::RenderRequest;

// Internal — invoked by RenderService.
class PdfPageRenderer {
public:
    bool renderPages(const PdfReader& reader,
                     const RenderRequest& request,
                     std::vector<document::PageArtifact>& pages) const;
    bool renderPage(const PdfReader& reader,
                    const RenderRequest& request,
                    int page_index,
                    document::PageArtifact& page) const;

private:
    bool renderPageBitmap(const PdfReader& reader, int page_index, int dpi, document::PageBitmap& bitmap) const;
};

} // namespace doc_parser::pdf
