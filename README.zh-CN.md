# Document Intelligence Engine

[English](README.md)

Document Intelligence Engine 是一个面向 C++ 原生部署、backend-agnostic 的文档智能引擎。

它的目标不是再做一个以模型效果为中心的“PDF 转 Markdown 应用”，而是沉淀文档解析背后的工程基础设施：统一的数据模型、稳定的 Pipeline 边界、可插拔的后端适配、可追溯的中间产物、可复现的 C++ 构建，以及适合被业务系统嵌入的 SDK 内核。

OCR、版面分析、表格识别、ONNX 模型、视觉大模型、外部解析器都应该被视为可替换能力。项目真正长期稳定的部分，是把这些能力的输出归一化为统一的文档模型，并通过可测试、可调试、可扩展的流水线持续加工。

技术文档和表格密集型 PDF 是当前第一阶段的主战场。它们能逼出真实的工程需求：页码、坐标、章节层级、表格结构、参数/数值/单位关系、置信度、调试产物，以及面向 RAG 和业务系统的结构化输出。

## 项目目标

构建一个生产导向的 C++ 文档解析引擎，能够：

- 通过后端适配器打开和渲染文档。
- 将 PDF 文本层、OCR 结果、版面块、表格结构和模型输出归一化为统一的内部模型。
- 通过分阶段、可检查的 Pipeline 组合这些模型。
- 输出稳定的 JSON/Markdown，供下游系统消费。
- 保留页码、bbox、来源后端、置信度和 debug artifacts，保证结果可追溯。
- 先支持 CLI，长期形成稳定的 C++ SDK/library 边界。

当前 CLI 形态：

```bash
document_intelligence_engine input.pdf --out output/
```

输出目录示例：

```text
output/
  document.json
  document.md
  pages/
    page_1.png
    page_2.png
  debug/
```

目标 JSON 形态示例：

```json
{
  "pages": [
    {
      "page": 1,
      "blocks": [
        {
          "type": "title",
          "text": "Technical Specification",
          "bbox": [80, 40, 700, 90]
        },
        {
          "type": "table",
          "bbox": [90, 180, 760, 520],
          "data": [
            ["Parameter", "Value", "Unit"],
            ["Pressure", "1.6", "MPa"]
          ]
        }
      ]
    }
  ]
}
```

## 项目范围

这个项目优先做基础设施，而不是优先堆模型能力。

- **核心引擎**：C++17/CMake、原生依赖 RAII 封装、稳定领域模型、Pipeline 编排、导出契约、测试和 CI。
- **后端适配**：PDFium、OCR 引擎、Layout/Table 模型、ONNX Runtime、视觉大模型、外部文档解析器都可以通过窄接口接入。
- **第一阶段场景**：技术文档和表格密集型 PDF，包括规格书、参数表、检测报告、标准、手册和结构化表格文档。
- **可复现评估**：优先使用公开数据集和小规模精选技术文档样例，而不是一开始依赖大规模私有行业数据。

模型微调是后续选项，不是项目中心。当前最重要的是让解析流水线可靠、可扩展、可测试、可部署。

## 项目价值

很多开源文档解析项目更关注最终转换效果：把文档转成 Markdown、HTML、JSON 或适合大模型/RAG 的 chunks。Document Intelligence Engine 更关注底层解析引擎本身，让这些输出在 C++ 应用里稳定、可控、可扩展。

项目应该优先沉淀：

- **统一文档模型**：PDF 文本、OCR 文本、版面块、表格、阅读顺序、来源引用和最终文档结构，都进入同一套 typed model。
- **后端无关**：PDFium、OCR、Layout Detector、Table Recognizer、外部 Parser、VLM 都应该可替换，不应绑死 Pipeline。
- **C++ 原生部署**：核心能力可以作为 CLI、library 和未来 SDK，在私有化、离线、嵌入式场景中使用。
- **结果可追溯**：每个抽取结果都应该能回到页码、bbox、来源后端、置信度和 debug artifact。
- **工程质量**：可复现构建、清晰模块边界、RAII 所有权、单元测试、冒烟测试、稳定 schema 和 CI，与模型效果同等重要。
- **技术文档深度**：技术/表格文档是第一阶段 benchmark，因为它们天然要求坐标、表格结构、章节层级、单位和结构化抽取。

## 非目标

这个项目不应该正面竞争成一个大而全的模型型文档解析平台。

它不追求：

- 替代成熟的终端用户文档解析应用。
- 自己拥有所有 OCR、Layout、Table、VLM 模型。
- 成为 Python-first 的训练框架。
- 把 Markdown 转换当作唯一核心产品。
- 把所有中间决策隐藏在不可解释的模型黑盒里。

更合理的方向是：可以消费这些模型或应用的输出，通过 adapter 归一化到统一模型，再提供稳定的 C++ 文档解析引擎给下游产品使用。

## Pipeline 边界

解析器按阶段组织。每个阶段只承担窄职责，并通过 typed intermediate data 传递给下一阶段。具体实现可以演进，但阶段边界应保持稳定。

Pipeline 应该保持 backend-agnostic。PDFium、OCR 引擎、Layout 模型、Table 模型、外部 Parser 和 VLM 服务，都通过 adapter 进入系统，并归一化为同一套 document model。

```text
document input
  -> source ingestion
  -> page rendering / page artifacts
  -> text extraction
  -> layout analysis
  -> table structure recovery
  -> document assembly
  -> export / SDK result
```

### 阶段职责

**Source ingestion**

负责后端初始化和文档访问。当前实现通过 PDFium 打开 PDF，管理 PDFium 生命周期，读取页数和页级元数据，并把 PDFium 资源管理细节隔离在 PDFium backend 模块内部。未来可以增加其他文档后端或外部解析器输出适配器。

**Page rendering**

将页面渲染为图像和页面 artifact。它不做 OCR、不做版面分析、不做表格重建。

当前输出：

```text
pages/page_1.png
pages/page_2.png
```

**Text extraction**

提供统一文本提取接口。Pipeline 应该调用一个 text extraction stage，而不是在业务层直接判断 PDFium、OCR 或其他模型。

当前策略：

```text
PDF text layer -> PageText
```

未来策略：

```text
if PDF text layer is usable:
  use PDF text layer
else:
  use OCR
```

PDF 文本层和 OCR 都必须归一化到相同的内部文本模型：

```text
TextSpan -> TextLine -> PageText
```

这个阶段只提供文本、坐标、置信度和来源信息，不判断标题、段落、表格或图片语义。

**Layout analysis**

将页面区域分类为语义块，例如：

```text
title
text
table
figure
header
footer
```

Layout 消费页面图像和归一化文本，输出带 bbox 和 confidence 的 layout blocks。它不负责重建表格单元格，也不负责导出 Markdown。

**Table structure recovery**

消费 table layout block 以及落在表格区域内的文本 token，负责恢复行、列、单元格、合并单元格和表格阅读顺序。

它不应该关心文本来自 PDF text layer 还是 OCR，只消费归一化后的文本和坐标。

**Document assembly**

组合 layout blocks、text tokens、table structures、page metadata 和 reading order，生成结构化文档树。

这是第一个创建面向用户文档结构的阶段：

```text
pages
  blocks
    title/text/table/figure
```

**Export / SDK result**

负责输出最终给消费者使用的结果：

```text
document.json
document.md
```

普通输出不应默认暴露原始中间数据。`PageText`、OCR boxes、layout proposals、table debug cells 等中间信息应该只在 debug 模式或显式诊断输出中保留。

## 中间数据策略

中间模型用于阶段之间通信，不是公开输出契约。

例如：

```text
PageText      text extraction -> layout/table
LayoutBlock   layout -> document assembly/table
TableCell     table recovery -> document assembly/export
```

普通输出应该是 assembled document result。debug 输出可以包含中间数据，用于检查、调试和回归测试。

## 模块布局

规划中的模块布局：

```text
cpp/
  app/          CLI entrypoint
  pipeline/     Pipeline 编排和 stage interfaces
  document/     共享内部文档模型
  backend/
    pdf/        PDF backend facade，负责文档访问、渲染、文本提取
      pdfium/   PDFium-specific adapter 和 native 资源管理
  image/        OpenCV preprocessing
  ocr/          OCR adapters and text normalization
  layout/       Layout block detection
  table/        Table structure recovery
  inference/    ONNX Runtime inference wrappers
  export/       JSON and Markdown writers

python/
  ocr_train/     OCR experiments and fine-tuning
  layout_train/  Layout model training
  export_onnx/   Model export scripts

data/          Dataset adapters and small samples
models/        Local model files, not committed
docs/          Design notes and evaluation reports
docker/        Deployment assets
tests/         Unit and integration tests
```

## 公开数据集

项目应尽量围绕公开数据集构建，保证工作可复现：

- [DocLayNet](https://github.com/DS4SD/DocLayNet)：版面检测数据集，覆盖 manuals、patents、tenders、financial reports、laws、scientific articles 等文档类别。
- [PubTables-1M](https://github.com/microsoft/table-transformer)：大规模表格检测和表格结构识别数据集。
- [FUNSD](https://guillaumejaume.github.io/FUNSD/)：小规模扫描表单理解数据集，可用于 OCR、实体和关系实验。

Demo 和针对性评估可以使用一小组公开技术 PDF。

## 实现里程碑

### Phase 1: PDF Ingestion

- 维护可复现的 C++17/CMake 构建。
- 通过 pinned setup script 集成 PDFium。
- 打开 PDF，读取页级元数据。
- 渲染页面图像。
- 生成最小 JSON/Markdown 输出。

### Phase 2: Image Preprocessing

- 引入 OpenCV preprocessing。
- 实现灰度、二值化、去噪和 deskew。
- 保留中间 debug artifacts，支持检查和回归测试。

### Phase 3: OCR and Text Reconstruction

- 接入 OCR baseline。
- 归一化 words、lines、confidence 和 bounding boxes。
- 重建 reading order，并合并 OCR 文本到 page-level structures。

### Phase 4: Layout Analysis

- 基于 DocLayNet 做 layout detection 实验。
- 将 layout regions 归一化为 title、text、table、figure、list、header、footer 等 block。
- 结合 layout regions 和 OCR/PDF text。

### Phase 5: Table Structure Recovery

- 使用 PubTables-1M 或 Table Transformer 作为 table baseline。
- 实现 rule-based `TableStructureBuilder`，支持线框表格和对齐文本表格。
- 将表格导出为 JSON 和 Markdown。

### Phase 6: Deployment and Evaluation

- 按需导出模型到 ONNX。
- 增加 C++ ONNX Runtime wrappers。
- 增加 benchmark scripts 和 performance reports。
- 增加 Docker packaging，以及可选 HTTP/gRPC service。

## 当前状态

项目仍处于早期实现阶段。当前已经具备 C++17/CMake CLI、pinned PDFium setup、PDFium 生命周期管理、PDF 打开和页数读取、页面渲染、内部文本模型、PDF text layer 提取、OpenCV 图像预处理、可选 Tesseract OCR baseline、基础 layout analysis、基础 table recognition、JSON manifest 输出，以及主流程冒烟测试。

当前 pipeline 仍然很小：

```text
PDF -> rendered pages -> PageText -> PageLayout -> PageTables -> minimal manifest
```

后续实现应继续保持清晰 stage 边界，继续提升 layout/table recognition 质量，再逐步加入更稳定的 export/SDK layer。

## 构建

配置和编译：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target document_intelligence_engine --parallel
./build/cpp/app/document_intelligence_engine input.pdf --out output/
```

可以显式选择各阶段 backend，同时保持统一 pipeline 输出契约：

```bash
./build/cpp/app/document_intelligence_engine input.pdf --out output/ \
  --document-backend pdfium \
  --ocr-backend auto \
  --layout-backend text \
  --table-backend text
```

PDFium 缺失时会在 CMake configure 阶段自动下载。固定版本会安装到 `third_party/pdfium`，该目录不会提交到 git。

如果本机没有 OpenCV，可以关闭图像预处理：

```bash
cmake -S . -B build -DDOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV=OFF
```

## 开发文档

- Dependency setup notes: [docs/dependencies.md](docs/dependencies.md)
- Commit message convention: [docs/commit-convention.md](docs/commit-convention.md)
- Development plan: [docs/roadmap.md](docs/roadmap.md)

## License

本项目使用 [MIT License](LICENSE)。
