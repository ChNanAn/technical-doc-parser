#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/text_model.h"
#include "layout/layout_backend.h"

#include <memory>

namespace doc_parser::layout {

class LayoutService {
public:
    LayoutService();
    explicit LayoutService(const ILayoutBackend& backend);
    explicit LayoutService(std::unique_ptr<ILayoutBackend> backend);

    bool
    analyze(const document::PageArtifact& page, const document::PageText& text, document::PageLayout& layout) const;

private:
    std::unique_ptr<ILayoutBackend> owned_backend_;
    const ILayoutBackend* backend_ = nullptr;
};

} // namespace doc_parser::layout
