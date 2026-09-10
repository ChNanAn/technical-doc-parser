#pragma once

#include "document/page_artifact.h"
#include "document/page_rotation.h"

namespace doc_parser::image {

// Private lossless processing image. The published source and shared cache entries
// stay immutable. File-backed views also support backends that read paths directly.
// Keep the view alive through all page consumers, including document-level ordering.
class OrientedPageView {
public:
    OrientedPageView() = default;
    ~OrientedPageView();
    OrientedPageView(const OrientedPageView&) = delete;
    OrientedPageView& operator=(const OrientedPageView&) = delete;
    bool prepare(const document::PageArtifact& source,
                 const document::PageRotation& rotation,
                 const std::filesystem::path& work_root);
    const document::PageArtifact& page() const { return page_; }

private:
    document::PageArtifact page_;
    std::filesystem::path directory_;
};

} // namespace doc_parser::image
