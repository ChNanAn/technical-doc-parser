#include "reading_order/reading_order_service.h"

#include <utility>

namespace doc_parser::reading_order {

ReadingOrderService::ReadingOrderService(const IReadingOrderBackend& backend) : backend_(&backend) {}

ReadingOrderService::ReadingOrderService(std::unique_ptr<IReadingOrderBackend> backend)
    : owned_backend_(std::move(backend)), backend_(owned_backend_.get()) {}

bool ReadingOrderService::order(const document::PageArtifact& page,
                                const document::PageLayout& layout,
                                document::PageReadingOrder& reading_order) const {
    reading_order = {};
    reading_order.page_index = page.page_index;
    reading_order.page_number = page.page_number;

    if (backend_ == nullptr) {
        return false;
    }

    ReadingOrderResult result;
    if (!backend_->order({page, layout}, result)) {
        return false;
    }

    reading_order = result.reading_order;
    return true;
}

} // namespace doc_parser::reading_order
