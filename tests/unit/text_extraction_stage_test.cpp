#include "backend/document_backend_interfaces.h"
#include "document/page_artifact.h"
#include "document/text_model.h"
#include "ocr/ocr_backend.h"
#include "pipeline/pipeline_context.h"
#include "pipeline/text_extraction_stage.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

class FakeNativeTextExtractor final : public doc_parser::pipeline::INativeTextExtractor {
public:
    bool extractNativeText(const doc_parser::pipeline::PipelineContext& context,
                           std::vector<doc_parser::document::PageText>& page_texts) const override {
        if (context.render.dpi <= 0) {
            return false;
        }
        ++extract_native_text_calls;
        page_texts = native_texts;
        return true;
    }

    std::vector<doc_parser::document::PageText> native_texts;
    mutable int extract_native_text_calls = 0;
};

class RecordingOcrBackend final : public doc_parser::ocr::IOcrBackend {
public:
    bool recognize(const doc_parser::ocr::OcrRequest& request, doc_parser::ocr::OcrResult& result) const override {
        ++recognize_calls;
        last_page_number = request.page.page_number;
        last_dpi = request.dpi;

        doc_parser::document::TextLine line;
        line.text = "ocr text";
        line.source = doc_parser::document::TextSource::Ocr;

        result.page_text = {};
        result.page_text.page_index = request.page.page_index;
        result.page_text.page_number = request.page.page_number;
        result.page_text.has_text = true;
        result.page_text.preferred_source = doc_parser::document::TextSource::Ocr;
        result.page_text.lines.push_back(line);
        return true;
    }

    mutable int recognize_calls = 0;
    mutable int last_page_number = 0;
    mutable int last_dpi = 0;
};

doc_parser::document::PageArtifact makePageArtifact() {
    doc_parser::document::PageArtifact page;
    page.page_index = 0;
    page.page_number = 1;
    page.output_path = std::filesystem::path("/tmp/page_1.png");
    return page;
}

doc_parser::pipeline::PipelineContext makeContext() {
    doc_parser::pipeline::PipelineContext context;
    context.render.dpi = 200;
    return context;
}

} // namespace

TEST(TextExtractionStageTest, KeepsNativeTextWhenPresent) {
    FakeNativeTextExtractor native_text_extractor;

    doc_parser::document::PageText native_text;
    native_text.page_index = 0;
    native_text.page_number = 1;
    native_text.has_text = true;
    native_text.preferred_source = doc_parser::document::TextSource::PdfTextLayer;

    doc_parser::document::TextLine line;
    line.text = "native text";
    line.source = doc_parser::document::TextSource::PdfTextLayer;
    native_text.lines.push_back(line);

    native_text_extractor.native_texts = {native_text};

    const RecordingOcrBackend ocr_backend;
    const doc_parser::pipeline::TextExtractionStage stage(&native_text_extractor, ocr_backend);

    std::vector<doc_parser::document::PageText> page_texts;
    EXPECT_TRUE(stage.extract(makeContext(), {makePageArtifact()}, page_texts).okStatus());

    EXPECT_EQ(native_text_extractor.extract_native_text_calls, 1);
    EXPECT_EQ(ocr_backend.recognize_calls, 0);
    ASSERT_EQ(page_texts.size(), 1U);
    EXPECT_TRUE(page_texts[0].has_text);
    ASSERT_EQ(page_texts[0].lines.size(), 1U);
    EXPECT_EQ(page_texts[0].lines[0].text, "native text");
    EXPECT_EQ(page_texts[0].preferred_source, doc_parser::document::TextSource::PdfTextLayer);
}

TEST(TextExtractionStageTest, UsesOcrBackendWhenNativeTextIsEmpty) {
    FakeNativeTextExtractor native_text_extractor;

    doc_parser::document::PageText empty_native_text;
    empty_native_text.page_index = 0;
    empty_native_text.page_number = 1;
    empty_native_text.has_text = false;
    empty_native_text.preferred_source = doc_parser::document::TextSource::Unknown;
    native_text_extractor.native_texts = {empty_native_text};

    const RecordingOcrBackend ocr_backend;
    const doc_parser::pipeline::TextExtractionStage stage(&native_text_extractor, ocr_backend);

    std::vector<doc_parser::document::PageText> page_texts;
    EXPECT_TRUE(stage.extract(makeContext(), {makePageArtifact()}, page_texts).okStatus());

    EXPECT_EQ(native_text_extractor.extract_native_text_calls, 1);
    EXPECT_EQ(ocr_backend.recognize_calls, 1);
    EXPECT_EQ(ocr_backend.last_page_number, 1);
    EXPECT_EQ(ocr_backend.last_dpi, 200);

    ASSERT_EQ(page_texts.size(), 1U);
    EXPECT_TRUE(page_texts[0].has_text);
    EXPECT_EQ(page_texts[0].preferred_source, doc_parser::document::TextSource::Ocr);
    ASSERT_EQ(page_texts[0].lines.size(), 1U);
    EXPECT_EQ(page_texts[0].lines[0].text, "ocr text");
}

TEST(TextExtractionStageTest, UsesOcrBackendWhenNativeTextExtractorIsUnavailable) {
    const RecordingOcrBackend ocr_backend;
    const doc_parser::pipeline::TextExtractionStage stage(nullptr, ocr_backend);

    std::vector<doc_parser::document::PageText> page_texts;
    EXPECT_TRUE(stage.extract(makeContext(), {makePageArtifact()}, page_texts).okStatus());

    EXPECT_EQ(ocr_backend.recognize_calls, 1);
    ASSERT_EQ(page_texts.size(), 1U);
    EXPECT_TRUE(page_texts[0].has_text);
    EXPECT_EQ(page_texts[0].preferred_source, doc_parser::document::TextSource::Ocr);
}
