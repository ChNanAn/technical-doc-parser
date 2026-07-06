#include "layout/layout_service.h"

#include <utility>

namespace doc_parser::layout {
namespace {

const ILayoutBackend& defaultLayoutBackend() {
    static const TextLayoutModelBackend backend;
    return backend;
}

} // namespace

LayoutService::LayoutService() : LayoutService(defaultLayoutBackend()) {}

LayoutService::LayoutService(const ILayoutBackend& backend) : backend_(&backend) {}

LayoutService::LayoutService(std::unique_ptr<ILayoutBackend> backend)
    : owned_backend_(std::move(backend)), backend_(owned_backend_.get()) {}

bool LayoutService::analyze(const document::PageArtifact& page,
                            const document::PageText& text,
                            document::PageLayout& layout) const {
    layout = {};
    layout.page_index = page.page_index;
    layout.page_number = page.page_number;

    if (backend_ == nullptr) {
        return false;
    }

    LayoutResult result;
    if (!backend_->analyze({page, text}, result)) {
        return false;
    }

    layout = result.layout;
    return true;
}

} // namespace doc_parser::layout
