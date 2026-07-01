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

// 渲染操作。无状态，操作通过 const PdfReader& 接收 PDF 源。
class RenderService {
public:
    RenderService() = default;

    bool renderPages(const PdfReader& source,
                     const RenderRequest& request,
                     std::vector<RenderedPage>& pages) const;
};

}  // namespace doc_parser::pdf
