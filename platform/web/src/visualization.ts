export type StageName =
  | "render"
  | "text"
  | "layout"
  | "table"
  | "reading_order"
  | "assembly"
  | "export";

export type BBox = [number, number, number, number];

export type OverlayItem = {
  id: string;
  kind: string;
  label: string;
  text: string;
  bbox: BBox;
  confidence?: number;
  order?: number;
  raw: unknown;
};

export function asRecord(value: unknown): Record<string, unknown> | undefined {
  return typeof value === "object" && value !== null && !Array.isArray(value)
    ? value as Record<string, unknown>
    : undefined;
}

export function asArray(value: unknown): unknown[] {
  return Array.isArray(value) ? value : [];
}

function asNumber(value: unknown): number | undefined {
  return typeof value === "number" && Number.isFinite(value) ? value : undefined;
}

function asString(value: unknown): string {
  return typeof value === "string" ? value : "";
}

export function bboxFrom(value: unknown): BBox | undefined {
  if (Array.isArray(value) && value.length === 4) {
    const coordinates = value.map(asNumber);
    if (coordinates.every((coordinate) => coordinate !== undefined)) {
      const [x0, y0, x1, y1] = coordinates as BBox;
      return x1 > x0 && y1 > y0 ? [x0, y0, x1, y1] : undefined;
    }
  }

  const record = asRecord(value);
  if (!record) return undefined;
  const x0 = asNumber(record.x0);
  const y0 = asNumber(record.y0);
  const x1 = asNumber(record.x1);
  const y1 = asNumber(record.y1);
  if (x0 === undefined || y0 === undefined || x1 === undefined || y1 === undefined) return undefined;
  return x1 > x0 && y1 > y0 ? [x0, y0, x1, y1] : undefined;
}

function confidenceFrom(record: Record<string, unknown>): number | undefined {
  const direct = asNumber(record.confidence);
  if (direct !== undefined) return direct;
  return asNumber(asRecord(record.score)?.value);
}

function pageOutput(stageOutput: unknown, pageNumber: number): Record<string, unknown> | undefined {
  const entry = asArray(stageOutput)
    .map(asRecord)
    .find((candidate) => candidate?.page_number === pageNumber);
  return asRecord(entry?.output);
}

function textForLayoutBlock(
  block: Record<string, unknown>,
  textOutput: unknown,
  pageNumber: number,
): string {
  const lines = asArray(pageOutput(textOutput, pageNumber)?.lines).map(asRecord);
  return asArray(block.text_line_indices)
    .map(asNumber)
    .filter((index): index is number => index !== undefined)
    .map((index) => asString(lines[index]?.text))
    .filter(Boolean)
    .join("\n");
}

function item(
  raw: Record<string, unknown>,
  fallbackId: string,
  fallbackKind: string,
  fallbackText = "",
): OverlayItem | undefined {
  const bbox = bboxFrom(raw.bbox);
  if (!bbox) return undefined;
  const kind = asString(raw.type) || fallbackKind;
  const text = asString(raw.text) || fallbackText;
  return {
    id: asString(raw.id) || fallbackId,
    kind,
    label: kind,
    text,
    bbox,
    confidence: confidenceFrom(raw),
    raw,
  };
}

function textItems(stageOutput: unknown, pageNumber: number): OverlayItem[] {
  return asArray(pageOutput(stageOutput, pageNumber)?.lines)
    .map(asRecord)
    .map((line, index) => line && item(line, `text-line-${pageNumber}-${index}`, "text"))
    .filter((value): value is OverlayItem => value !== undefined);
}

function layoutItems(stageOutput: unknown, textOutput: unknown, pageNumber: number): OverlayItem[] {
  return asArray(pageOutput(stageOutput, pageNumber)?.blocks)
    .map(asRecord)
    .map((block, index) => {
      if (!block) return undefined;
      return item(
        block,
        `layout-block-${pageNumber}-${index}`,
        "unknown",
        textForLayoutBlock(block, textOutput, pageNumber),
      );
    })
    .filter((value): value is OverlayItem => value !== undefined);
}

function tableItems(stageOutput: unknown, pageNumber: number): OverlayItem[] {
  const overlays: OverlayItem[] = [];
  asArray(pageOutput(stageOutput, pageNumber)?.tables)
    .map(asRecord)
    .forEach((table, tableIndex) => {
      if (!table) return;
      const tableId = asString(table.id) || `table-${pageNumber}-${tableIndex}`;
      const tableItem = item(table, tableId, "table");
      if (tableItem) overlays.push(tableItem);

      asArray(table.rows).map(asRecord).forEach((row, rowIndex) => {
        asArray(row?.cells).map(asRecord).forEach((cell, cellIndex) => {
          if (!cell) return;
          const cellItem = item(
            cell,
            `${tableId}-cell-${rowIndex}-${cellIndex}`,
            "cell",
          );
          if (cellItem) overlays.push(cellItem);
        });
      });
    });
  return overlays;
}

function readingOrderItems(
  stageOutput: unknown,
  layoutOutput: unknown,
  textOutput: unknown,
  pageNumber: number,
): OverlayItem[] {
  const layoutBlocks = asArray(pageOutput(layoutOutput, pageNumber)?.blocks).map(asRecord);
  return asArray(pageOutput(stageOutput, pageNumber)?.items)
    .map(asRecord)
    .map((orderItem): OverlayItem | undefined => {
      if (!orderItem) return undefined;
      const blockIndex = asNumber(orderItem.layout_block_index);
      const blockId = asString(orderItem.layout_block_id);
      const block = blockIndex !== undefined
        ? layoutBlocks[blockIndex]
        : layoutBlocks.find((candidate) => asString(candidate?.id) === blockId);
      if (!block) return undefined;
      const overlay = item(
        block,
        blockId,
        "unknown",
        textForLayoutBlock(block, textOutput, pageNumber),
      );
      if (!overlay) return undefined;
      const sequence = asNumber(orderItem.sequence_index) ?? 0;
      return {
        ...overlay,
        id: `order-${pageNumber}-${sequence}-${overlay.id}`,
        order: sequence,
        raw: { order: orderItem, block },
      };
    })
    .filter((value): value is OverlayItem => value !== undefined);
}

function documentBlockItems(stageOutput: unknown, pageNumber: number): OverlayItem[] {
  return asArray(asRecord(stageOutput)?.blocks)
    .map(asRecord)
    .filter((block) => (
      block?.page_id === `page_${pageNumber}` ||
      block?.page_number === pageNumber
    ))
    .map((block, index) => block && item(
      block,
      `document-block-${pageNumber}-${index}`,
      "unknown",
    ))
    .filter((value): value is OverlayItem => value !== undefined);
}

export function overlaysForStage(
  stage: StageName,
  stageOutput: unknown,
  pageNumber: number,
  layoutOutput?: unknown,
  textOutput?: unknown,
): OverlayItem[] {
  switch (stage) {
    case "text":
      return textItems(stageOutput, pageNumber);
    case "layout":
      return layoutItems(stageOutput, textOutput, pageNumber);
    case "table":
      return tableItems(stageOutput, pageNumber);
    case "reading_order":
      return readingOrderItems(stageOutput, layoutOutput, textOutput, pageNumber);
    case "assembly":
    case "export":
      return documentBlockItems(stageOutput, pageNumber);
    case "render":
      return [];
  }
}

export function warningFromEvent(event: Record<string, unknown>): Record<string, unknown> | undefined {
  return event.type === "stage_warning" ? asRecord(event.warning) : undefined;
}
