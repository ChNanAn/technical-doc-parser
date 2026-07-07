#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/reading_order_model.h"
#include "reading_order/reading_order_backend.h"

#include <memory>

namespace doc_parser::reading_order {

class ReadingOrderService {
public:
    explicit ReadingOrderService(const IReadingOrderBackend& backend);
    explicit ReadingOrderService(std::unique_ptr<IReadingOrderBackend> backend);

    bool order(const document::PageArtifact& page,
               const document::PageLayout& layout,
               document::PageReadingOrder& reading_order) const;

private:
    std::unique_ptr<IReadingOrderBackend> owned_backend_;
    const IReadingOrderBackend* backend_ = nullptr;
};

} // namespace doc_parser::reading_order
