#pragma once

#include "common/status.h"

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/table_model.h"
#include "document/text_model.h"
#include "pipeline/pipeline_context.h"
#include "table/table_backend.h"

#include <vector>

namespace doc_parser::pipeline {

class TableRecognitionStage {
public:
    explicit TableRecognitionStage(const table::ITableBackend& table);

    common::Status recognize(const PipelineContext& context,
                             const std::vector<document::PageArtifact>& pages,
                             const std::vector<document::PageText>& page_texts,
                             const std::vector<document::PageLayout>& page_layouts,
                             std::vector<document::PageTables>& page_tables) const;

private:
    const table::ITableBackend& table_;
};

} // namespace doc_parser::pipeline
