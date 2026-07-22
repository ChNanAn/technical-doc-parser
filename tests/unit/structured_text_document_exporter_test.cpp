#include "export/structured_text_document_exporter.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

doc_parser::document::TableCell
cell(int row, int column, std::string text, int row_span = 1, int column_span = 1, bool is_header = false) {
    doc_parser::document::TableCell value;
    value.row_index = row;
    value.column_index = column;
    value.row_span = row_span;
    value.column_span = column_span;
    value.is_header = is_header;
    value.text = std::move(text);
    return value;
}

doc_parser::document::ParsedDocument makeCrossPageTable() {
    doc_parser::document::ParsedDocument document;
    document.source.path = "fixture.pdf";

    doc_parser::document::DocumentBlock first;
    first.type = doc_parser::document::DocumentBlockType::Table;
    first.table_id = "page_1_table_1";
    first.table_continuation_group_id = "table_group_1";
    first.table_continues_on_next_page = true;
    doc_parser::document::TableRow header;
    header.row_index = 0;
    header.is_header = true;
    header.cells.push_back(cell(0, 0, "Parameter", 1, 2, true));
    first.table_rows.push_back(header);
    doc_parser::document::TableRow first_data;
    first_data.row_index = 1;
    first_data.cells.push_back(cell(1, 0, "Voltage"));
    first_data.cells.push_back(cell(1, 1, "5 V"));
    first.table_rows.push_back(first_data);

    doc_parser::document::DocumentBlock second = first;
    second.table_id = "page_2_table_1";
    second.table_continues_from_previous_page = true;
    second.table_continues_on_next_page = false;
    second.table_rows.clear();
    second.table_rows.push_back(header);
    doc_parser::document::TableRow second_data;
    second_data.row_index = 1;
    second_data.cells.push_back(cell(1, 0, "Current"));
    second_data.cells.push_back(cell(1, 1, "2 A"));
    second.table_rows.push_back(second_data);

    document.blocks.push_back(std::move(first));
    document.blocks.push_back(std::move(second));
    return document;
}

} // namespace

TEST(StructuredTextDocumentExporterTest, PreservesMergedCellsAndCombinesCrossPageTables) {
    const std::filesystem::path markdown_path = std::filesystem::temp_directory_path() / "tdp_structured_table_test.md";
    const std::filesystem::path html_path = std::filesystem::temp_directory_path() / "tdp_structured_table_test.html";
    const doc_parser::document::ParsedDocument document = makeCrossPageTable();

    ASSERT_TRUE(doc_parser::exporter::MarkdownDocumentExporter().write({false, markdown_path, &document, nullptr}));
    ASSERT_TRUE(doc_parser::exporter::HtmlDocumentExporter().write({false, html_path, &document, nullptr}));

    const std::string markdown = readFile(markdown_path);
    const std::string html = readFile(html_path);
    EXPECT_NE(markdown.find("colspan=\"2\""), std::string::npos);
    EXPECT_EQ(markdown.find("Parameter"), markdown.rfind("Parameter"));
    EXPECT_NE(markdown.find("Voltage"), std::string::npos);
    EXPECT_NE(markdown.find("Current"), std::string::npos);
    EXPECT_NE(html.find("<th colspan=\"2\">Parameter</th>"), std::string::npos);
    EXPECT_EQ(html.find("<table>"), html.rfind("<table>"));
    EXPECT_EQ(html.find("Parameter"), html.rfind("Parameter"));

    std::filesystem::remove(markdown_path);
    std::filesystem::remove(html_path);
}
