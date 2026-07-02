#pragma once

#include "document/page_artifact.h"

#include <filesystem>
#include <vector>

namespace doc_parser::pdf {

class PdfReader;
struct RenderRequest;

// Internal — invoked by RenderService.
class PdfPageRenderer {
public:
    bool renderPages(const PdfReader& reader,
                     const RenderRequest& request,
                     std::vector<document::PageArtifact>& pages) const;

private:
    bool renderPageBitmap(const PdfReader& reader, int page_index, int dpi, document::PageBitmap& bitmap) const;
};

} // namespace doc_parser::pdf
