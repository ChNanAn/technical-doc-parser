#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/table_model.h"
#include "document/text_model.h"
#include "pipeline/pipeline_context.h"
#include "pipeline/stage_result.h"
#include "table/table_backend.h"

#include <vector>

namespace doc_parser::pipeline {

class TableRecognitionStage {
public:
    explicit TableRecognitionStage(const table::ITableBackend& table);

    StageResult<std::vector<document::PageTables>> recognize(const PipelineContext& context,
                                                             const std::vector<document::PageArtifact>& pages,
                                                             const std::vector<document::PageText>& page_texts,
                                                             std::vector<document::PageLayout>& page_layouts) const;

private:
    const table::ITableBackend& table_;
};

} // namespace doc_parser::pipeline
