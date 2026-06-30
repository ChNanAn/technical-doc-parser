# Internal Text Model

The parser normalizes text from different extraction paths into one internal model before layout analysis, table reconstruction, or downstream export.

## Responsibility Boundary

PDF text layer and OCR both provide text tokens with coordinates. They should not decide whether a region is a title, paragraph, table, or figure.

Layout analysis classifies regions:

```text
title
text
table
figure
header
footer
```

Table reconstruction consumes text tokens that fall inside a table block. It should not know whether those tokens came from the PDF text layer or OCR.

## Model

The C++ contract lives in:

```text
cpp/document/text_model.h
```

Core types:

```text
BBox
TextSpan
TextLine
PageText
```

`TextSpan` is the smallest normalized text unit used by later stages. It keeps the text, bounding box, confidence, and source.

`TextLine` groups spans that belong to the same visual line.

`PageText` is the per-page normalized text input consumed by layout and table stages.

## Source

The `source` field is kept for debugging and quality checks:

```text
Unknown
PdfTextLayer
Ocr
```

Digital PDFs should prefer `PdfTextLayer` when the extracted text is usable. Scanned pages should fall back to `Ocr`.

Later stages should consume the normalized `PageText` model rather than branching on extraction source.

## Planned Flow

```text
PDF text layer  -> TextSpan/TextLine/PageText
OCR             -> TextSpan/TextLine/PageText

PageText + page image -> Layout blocks
Layout table block + PageText tokens -> Table structure
```

This keeps source extraction, layout classification, and table reconstruction independently testable.
