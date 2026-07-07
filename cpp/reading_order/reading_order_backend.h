#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/reading_order_model.h"

namespace doc_parser::reading_order {

struct ReadingOrderRequest {
    const document::PageArtifact& page;
    const document::PageLayout& layout;
};

struct ReadingOrderResult {
    document::PageReadingOrder reading_order;
};

class IReadingOrderBackend {
public:
    virtual ~IReadingOrderBackend() = default;

    virtual bool order(const ReadingOrderRequest& request, ReadingOrderResult& result) const = 0;
};

class DoclingLikeReadingOrderBackend final : public IReadingOrderBackend {
public:
    bool order(const ReadingOrderRequest& request, ReadingOrderResult& result) const override;
};

} // namespace doc_parser::reading_order
