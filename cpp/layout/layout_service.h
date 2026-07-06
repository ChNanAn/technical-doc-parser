#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/text_model.h"
#include "layout/layout_backend.h"

namespace doc_parser::layout {

class LayoutService {
public:
    LayoutService();
    explicit LayoutService(const ILayoutBackend& backend);

    bool
    analyze(const document::PageArtifact& page, const document::PageText& text, document::PageLayout& layout) const;

private:
    const ILayoutBackend* backend_ = nullptr;
};

} // namespace doc_parser::layout
