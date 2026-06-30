#include "document/text_model.h"

#include <cassert>

int main() {
    doc_parser::document::TextSpan span;
    span.text = "DN50";
    span.bbox = {10.0, 20.0, 60.0, 35.0};
    span.source = doc_parser::document::TextSource::PdfTextLayer;

    doc_parser::document::TextLine line;
    line.text = span.text;
    line.bbox = span.bbox;
    line.source = span.source;
    line.spans.push_back(span);

    doc_parser::document::PageText page_text;
    page_text.page_index = 0;
    page_text.page_number = 1;
    page_text.has_text = true;
    page_text.preferred_source = doc_parser::document::TextSource::PdfTextLayer;
    page_text.lines.push_back(line);

    assert(page_text.has_text);
    assert(page_text.lines.size() == 1);
    assert(page_text.lines.front().spans.front().text == "DN50");
    assert(page_text.lines.front().spans.front().source == doc_parser::document::TextSource::PdfTextLayer);

    return 0;
}
