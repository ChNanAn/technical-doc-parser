#pragma once

#include <filesystem>
#include <vector>

namespace doc_parser::pdf {

// Forward decls — full definitions live in pdf/render_service.h
class PdfReader;
struct RenderRequest;
struct RenderedPage;

// Internal — invoked by RenderService.
class PdfPageRenderer {
public:
    bool renderPages(const PdfReader& reader, const RenderRequest& request, std::vector<RenderedPage>& pages) const;
};

} // namespace doc_parser::pdf
