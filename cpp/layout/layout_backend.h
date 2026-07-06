#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/text_model.h"

namespace doc_parser::layout {

struct LayoutRequest {
    const document::PageArtifact& page;
    const document::PageText& text;
};

struct LayoutResult {
    document::PageLayout layout;
};

class ILayoutBackend {
public:
    virtual ~ILayoutBackend() = default;

    virtual bool analyze(const LayoutRequest& request, LayoutResult& result) const = 0;
};

class TextLayoutModelBackend final : public ILayoutBackend {
public:
    bool analyze(const LayoutRequest& request, LayoutResult& result) const override;
};

} // namespace doc_parser::layout
