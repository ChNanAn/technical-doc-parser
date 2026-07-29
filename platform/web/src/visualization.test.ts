import { describe, expect, it } from "vitest";
import { bboxFrom, overlaysForStage } from "./visualization";

describe("bboxFrom", () => {
  it("accepts contract arrays and debug objects", () => {
    expect(bboxFrom([10, 20, 30, 40])).toEqual([10, 20, 30, 40]);
    expect(bboxFrom({ x0: 1, y0: 2, x1: 3, y1: 4 })).toEqual([1, 2, 3, 4]);
  });

  it("rejects empty or inverted boxes", () => {
    expect(bboxFrom([10, 20, 10, 40])).toBeUndefined();
    expect(bboxFrom({ x0: 5, y0: 6, x1: 4, y1: 8 })).toBeUndefined();
  });
});

describe("overlaysForStage", () => {
  const textOutput = [
    {
      page_number: 1,
      output: {
        lines: [
          { bbox: { x0: 10, y0: 20, x1: 100, y1: 40 }, text: "First line", confidence: 0.9 },
          { bbox: { x0: 10, y0: 50, x1: 120, y1: 70 }, text: "Second line", confidence: 0.8 },
        ],
      },
    },
  ];
  const layoutOutput = [
    {
      page_number: 1,
      output: {
        blocks: [
          {
            id: "block_1",
            type: "title",
            bbox: { x0: 8, y0: 18, x1: 125, y1: 72 },
            text_line_indices: [0, 1],
            confidence: 0.75,
          },
        ],
      },
    },
  ];

  it("builds text and layout overlays with human-readable text", () => {
    const text = overlaysForStage("text", textOutput, 1);
    expect(text).toHaveLength(2);
    expect(text[0]).toMatchObject({ kind: "text", text: "First line", confidence: 0.9 });

    const layout = overlaysForStage("layout", layoutOutput, 1, undefined, textOutput);
    expect(layout).toHaveLength(1);
    expect(layout[0]).toMatchObject({
      id: "block_1",
      kind: "title",
      text: "First line\nSecond line",
      confidence: 0.75,
    });
  });

  it("maps reading order items back to their layout regions", () => {
    const readingOrder = [
      {
        page_number: 1,
        output: {
          items: [{ layout_block_id: "block_1", layout_block_index: 0, sequence_index: 3 }],
        },
      },
    ];
    const overlays = overlaysForStage(
      "reading_order",
      readingOrder,
      1,
      layoutOutput,
      textOutput,
    );
    expect(overlays).toHaveLength(1);
    expect(overlays[0]).toMatchObject({ kind: "title", order: 3 });
  });

  it("shows table regions and cells while preserving table rows", () => {
    const tableOutput = [
      {
        page_number: 1,
        output: {
          tables: [
            {
              id: "table_1",
              bbox: { x0: 10, y0: 20, x1: 200, y1: 100 },
              rows: [
                {
                  cells: [
                    {
                      bbox: { x0: 10, y0: 20, x1: 100, y1: 50 },
                      text: "Header",
                    },
                  ],
                },
              ],
            },
          ],
        },
      },
    ];
    const overlays = overlaysForStage("table", tableOutput, 1);
    expect(overlays.map((overlay) => overlay.kind)).toEqual(["table", "cell"]);
    expect(overlays[0].raw).toMatchObject({ id: "table_1" });
  });

  it("filters final document blocks to the selected page", () => {
    const output = {
      blocks: [
        { id: "one", type: "paragraph", page_id: "page_1", bbox: [1, 2, 3, 4], text: "One" },
        { id: "two", type: "paragraph", page_id: "page_2", bbox: [1, 2, 3, 4], text: "Two" },
      ],
    };
    const overlays = overlaysForStage("export", output, 2);
    expect(overlays).toHaveLength(1);
    expect(overlays[0]).toMatchObject({ id: "two", text: "Two" });
  });
});
