#pragma once

#include "pdf/pdf_reader.h"

#include <filesystem>
#include <string>
#include <vector>

namespace doc_parser::pdf {

struct RenderRequest {
    int dpi = 200;
    std::filesystem::path output_root;
    std::filesystem::path pages_dir;
};

struct RenderedPage {
    int page_index = 0;
    int page_number = 0;
    std::string relative_image;
    std::filesystem::path output_path;
};

class PdfPageRenderer {
public:
    bool renderPages(
        const PdfReader& reader,
        const RenderRequest& request,
        std::vector<RenderedPage>& pages
    ) const;
};

}  // namespace doc_parser::pdf
