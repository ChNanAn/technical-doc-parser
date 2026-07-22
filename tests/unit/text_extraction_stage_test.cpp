#include "document/page_artifact.h"
#include "document/text_model.h"
#include "document_source/document_source_interfaces.h"
#include "ocr/ocr_backend.h"
#include "pipeline/pipeline_context.h"
#include "pipeline/text_extraction_stage.h"
#include "pipeline/text_quality.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

class FakeNativeTextExtractor final : public doc_parser::document_source::INativeTextExtractor {
public:
    bool extractNativeText(const doc_parser::document_source::NativeTextRequest& request,
                           std::vector<doc_parser::document::PageText>& page_texts) const override {
        if (request.dpi <= 0) {
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
        if (!succeed) {
            return false;
        }

        doc_parser::document::TextLine line;
        line.text = output_text;
        line.bbox = output_bbox;
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
    bool succeed = true;
    std::string output_text = "ocr text";
    doc_parser::document::BBox output_bbox;
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

TEST(TextExtractionStageTest, MergesOcrIntoSparseNativeText) {
    FakeNativeTextExtractor native_text_extractor;
    doc_parser::document::PageText native_text;
    native_text.page_index = 0;
    native_text.page_number = 1;
    native_text.has_text = true;
    native_text.preferred_source = doc_parser::document::TextSource::PdfTextLayer;
    doc_parser::document::TextLine native_line;
    native_line.text = "native header";
    native_line.bbox = {10.0, 10.0, 200.0, 30.0};
    native_line.source = doc_parser::document::TextSource::PdfTextLayer;
    native_text.lines.push_back(native_line);
    native_text_extractor.native_texts = {native_text};

    RecordingOcrBackend ocr_backend;
    ocr_backend.output_text = "scanned body";
    ocr_backend.output_bbox = {10.0, 400.0, 300.0, 430.0};
    const doc_parser::pipeline::TextExtractionStage stage(&native_text_extractor, ocr_backend);
    doc_parser::document::PageArtifact page = makePageArtifact();
    page.width = 800;
    page.height = 1000;

    std::vector<doc_parser::document::PageText> page_texts;
    EXPECT_TRUE(stage.extract(makeContext(), {page}, page_texts).okStatus());
    EXPECT_EQ(ocr_backend.recognize_calls, 1);
    ASSERT_EQ(page_texts.size(), 1U);
    ASSERT_EQ(page_texts[0].lines.size(), 2U);
    EXPECT_EQ(page_texts[0].lines[0].text, "native header");
    EXPECT_EQ(page_texts[0].lines[1].text, "scanned body");
    EXPECT_EQ(page_texts[0].preferred_source, doc_parser::document::TextSource::Mixed);
}

TEST(TextExtractionStageTest, KeepsUsableSparseNativeTextWhenOcrEnhancementFails) {
    FakeNativeTextExtractor native_text_extractor;
    doc_parser::document::PageText native_text;
    native_text.page_index = 0;
    native_text.page_number = 1;
    native_text.has_text = true;
    native_text.preferred_source = doc_parser::document::TextSource::PdfTextLayer;
    doc_parser::document::TextLine native_line;
    native_line.text = "native header";
    native_line.bbox = {10.0, 10.0, 200.0, 30.0};
    native_text.lines.push_back(native_line);
    native_text_extractor.native_texts = {native_text};

    RecordingOcrBackend ocr_backend;
    ocr_backend.succeed = false;
    const doc_parser::pipeline::TextExtractionStage stage(&native_text_extractor, ocr_backend);
    doc_parser::document::PageArtifact page = makePageArtifact();
    page.width = 800;
    page.height = 1000;

    std::vector<doc_parser::document::PageText> page_texts;
    EXPECT_TRUE(stage.extract(makeContext(), {page}, page_texts).okStatus());
    ASSERT_EQ(page_texts.size(), 1U);
    ASSERT_EQ(page_texts[0].lines.size(), 1U);
    EXPECT_EQ(page_texts[0].lines[0].text, "native header");
    EXPECT_EQ(page_texts[0].preferred_source, doc_parser::document::TextSource::PdfTextLayer);
}

TEST(TextExtractionStageTest, ReplacesSuspiciousNativeTextWithOcr) {
    FakeNativeTextExtractor native_text_extractor;
    doc_parser::document::PageText native_text;
    native_text.page_index = 0;
    native_text.page_number = 1;
    native_text.has_text = true;
    native_text.preferred_source = doc_parser::document::TextSource::PdfTextLayer;
    doc_parser::document::TextLine native_line;
    native_line.text = std::string("bad") + static_cast<char>(1);
    native_text.lines.push_back(native_line);
    native_text_extractor.native_texts = {native_text};

    const RecordingOcrBackend ocr_backend;
    const doc_parser::pipeline::TextExtractionStage stage(&native_text_extractor, ocr_backend);

    std::vector<doc_parser::document::PageText> page_texts;
    EXPECT_TRUE(stage.extract(makeContext(), {makePageArtifact()}, page_texts).okStatus());
    EXPECT_EQ(ocr_backend.recognize_calls, 1);
    ASSERT_EQ(page_texts.size(), 1U);
    ASSERT_EQ(page_texts[0].lines.size(), 1U);
    EXPECT_EQ(page_texts[0].lines[0].text, "ocr text");
    EXPECT_EQ(page_texts[0].preferred_source, doc_parser::document::TextSource::Ocr);
}

TEST(NativeTextQualityPolicyTest, DropsOverlappingOcrLinesDuringMerge) {
    doc_parser::document::PageText native_text;
    native_text.has_text = true;
    native_text.preferred_source = doc_parser::document::TextSource::PdfTextLayer;
    doc_parser::document::TextLine native_line;
    native_line.text = "native";
    native_line.bbox = {10.0, 10.0, 100.0, 30.0};
    native_text.lines.push_back(native_line);

    doc_parser::document::PageText ocr_text;
    ocr_text.has_text = true;
    doc_parser::document::TextLine duplicate_line;
    duplicate_line.text = "ocr duplicate";
    duplicate_line.bbox = {12.0, 11.0, 98.0, 29.0};
    ocr_text.lines.push_back(duplicate_line);

    const doc_parser::pipeline::NativeTextQualityPolicy policy;
    const doc_parser::pipeline::TextMergeResult result = policy.merge(native_text, ocr_text);
    EXPECT_EQ(result.added_ocr_lines, 0U);
    ASSERT_EQ(result.text.lines.size(), 1U);
    EXPECT_EQ(result.text.preferred_source, doc_parser::document::TextSource::PdfTextLayer);
}

TEST(TextExtractionStageTest, FailsClearlyWhenRequiredOcrIsUnavailable) {
    const doc_parser::ocr::UnavailableOcrBackend unavailable("test backend unavailable");
    const doc_parser::pipeline::TextExtractionStage stage(nullptr, unavailable);

    std::vector<doc_parser::document::PageText> page_texts;
    const doc_parser::common::Status status = stage.extract(makeContext(), {makePageArtifact()}, page_texts);
    EXPECT_FALSE(status.okStatus());
    EXPECT_EQ(status.code(), "text.ocr_failed");
    EXPECT_NE(status.message().find("test backend unavailable"), std::string::npos);
}
