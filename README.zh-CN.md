# Document Intelligence Engine

[English](README.md)

一个 C++ 原生、后端无关的文档智能引擎，面向结构化文档解析，当前优先处理技术文档和表格密集型 PDF。

项目通过类型化、可替换的 Backend 接口组合原生文本提取、OCR、版面分析、阅读顺序、表格结构识别和文档组装，输出 JSON、Markdown、HTML、页面图像及可选调试产物，供下游业务系统使用。

> 这是一个已经能够运行和评测的早期引擎，还不是成熟的企业级文档产品。仓库内提交的数据主要用于回归保护，
> 外部 Benchmark 分数也不应被理解为生产准确率结论。

## 为什么做这个项目

- **C++ 原生**：面向离线、私有化和嵌入式部署。
- **后端无关**：PDF、OCR、Layout、Table 提供方可以演进，不需要重写 Pipeline。
- **结构化输出**：文本、版面块、表格、阅读顺序、页码、bbox 和置信度进入统一类型模型。
- **可检查、可评测**：中间产物、公开 Fixture、指标、冒烟测试和 CI 都是引擎的一部分。
- **可嵌入 SDK**：`DocumentEngine` 可跨任务复用模型 Session，并将类型化解析结果与具体导出格式解耦。

## Pipeline

```text
Document
  -> Render
  -> Native Text / OCR
  -> Layout
  -> Table Structure
  -> Reading Order
  -> Document Assembly
  -> JSON / Markdown / HTML
```

当前 Backend 包括：

- PDFium：PDF 访问、页面渲染和原生文本提取。
- PaddleOCR ONNX、Tesseract：OCR。
- RF-DETR DocLayNet、Paddle PP-DocLayoutV3 和确定性的 Text Layout fallback。
- Table Transformer 和确定性的 Text Table fallback。
- Docling-like Reading Order baseline。

模型和提供方都是适配器。项目希望长期稳定的是归一化文档模型和分阶段 Pipeline。

## 快速开始

参考环境为 Ubuntu 24.04。先安装系统依赖，再使用 Release Preset 构建：

```bash
bash scripts/setup_ubuntu_dependencies.sh
cmake --preset core-release
cmake --build --preset core-release --parallel
ctest --preset core-release
```

配置期间会在缺失时下载并校验 PDFium、ONNX Runtime 和固定版本的 baseline 模型。自定义路径、关闭自动下载和轻量构建方式见[依赖说明](docs/dependencies.md)。
仓库只维护一条 C++ 包管理器路径：vcpkg；没有包管理器 toolchain 时，CMake
仍保留固定版本的 FetchContent 源码构建路径。

解析文档：

```bash
./build/core-release/cpp/app/document_intelligence_engine input.pdf --out output/
```

带 Tag 的版本还会提供源码归档、Ubuntu 24.04 x86-64 CLI bundle，以及不含模型的
CLI 容器。程序包与模型包独立版本化，下载后应使用发布页中的 `SHA256SUMS`
校验。具体边界和发布步骤见[发布说明](docs/releasing.md)。

将发布页中的模型包安装到程序旁边：

```bash
mkdir -p models
tar -xzf technical-doc-parser-models-0.1.0.tar.gz \
  -C models --strip-components=1
```

可以显式选择 Backend，也可以使用版本化 Registry 配置：

```bash
./build/core-release/cpp/app/document_intelligence_engine input.pdf --out output/ \
  --ocr-backend auto \
  --layout-backend auto \
  --table-backend auto \
  --backend-config config/backends.json
```

### C++ 嵌入

安装 Library 和带版本的 CMake Package：

```bash
cmake --install build/core-release --prefix build/sdk
```

下游项目可使用 `find_package(DocumentIntelligenceEngine CONFIG REQUIRED)`，并链接
`DocumentIntelligenceEngine::engine`。`DocumentEngine` 只需配置一次，并在多次调用之间复用模型 Session。
`DocumentEngine::parse()` 返回 `ParseResult`，其中包含归一化文档、Pipeline Artifacts、结构化 Status 和运行
Provenance；是否导出 JSON、Markdown 或 HTML 由调用方显式决定。可运行代码见
[嵌入示例](examples/embed_document.cpp)和独立的 [CMake 项目](examples/CMakeLists.txt)。

PDFium 以及启用时的 ONNX Runtime 仍是外部 Package 依赖。如果它们不在系统标准路径，下游配置时需要设置
`PDFium_DIR` 和 `ONNXRuntime_ROOT`。普通 SDK 安装不包含模型权重；应将独立版本的模型包解压到
`share/DocumentIntelligenceEngine/models`，或通过 `EngineConfig` 显式配置每个路径。可迁移的 Package
通过 `DocumentIntelligenceEngine_MODEL_DIR` 暴露预期模型目录。

硬失败通过 `ParseResult::status` 返回。当 Pipeline 在运行时 fallback 后仍能保留可用结果时，解析会成功，
但 `document.status` 为 `partial`，同时产生机器可读 Warning，并在 `ParseResult::provenance` 中记录 fallback。
调用方可通过 `DocumentParseOptions::run_id` 将解析结果关联到自己的任务或 Trace。

## 输出

```text
output/
  document.json
  document.md
  document.html
  pages/
    page_1.png
    page_2.png
  debug/                 # 使用 --debug 时生成
```

普通 JSON 输出遵循 Document Contract v1，包含按最终阅读顺序排列、可定位到页面的 Blocks。启用 `--debug`
后，归一化文本、Layout Blocks、Reading Order、表格结构和图像预处理产物会写入页面的命名空间
`extensions` 字段。`--run-id` 可把调用方提供的关联 ID 写入 `producer`；可用但发生降级的结果使用
`status: "partial"`，并通过 `warnings` 解释降级原因。

```json
{
  "$schema": "https://github.com/ChNanAn/technical-doc-parser/schemas/document.v1.schema.json",
  "schema_version": 1,
  "document_id": "doc_...",
  "status": "complete",
  "source": {"filename": "input.pdf", "media_type": "application/pdf"},
  "producer": {"name": "technical-doc-parser", "version": "0.1.0"},
  "coordinate_space": {"unit": "pixel", "origin": "top_left", "bbox_format": "xyxy", "dpi": 200},
  "pages": [{"id": "page_1", "number": 1, "width": 1654, "height": 2339}],
  "blocks": [
    {
      "id": "doc_page_1_block_1",
      "type": "paragraph",
      "page_id": "page_1",
      "bbox": [84.0, 132.0, 742.0, 168.0],
      "text": "Technical specification"
    }
  ],
  "warnings": []
}
```

第一版公共文档契约已提供 release candidate：[Document Contract v1](docs/document-contract-v1.zh-CN.md)。它只冻结
可扩展输出外壳和坐标语义，实际结果质量由独立的三层评测持续约束。

## 评测

仓库包含可再分发的 OCR、Layout 和 Table Fixture，以及与具体 Backend 无关的 Evaluator。完整模型构建包含真实 PaddleOCR、DocLayNet、Paddle Layout 和 Table Transformer 推理回归。

已提交的模型评测集规模有意保持较小，主要用于防止预处理、推理、标签映射和后处理发生回退；项目仍需要更广泛的技术文档验证。

第一次完整外部评测在 1,403 页 [olmOCR-Bench](docs/benchmarks/olmocr-bench.md) 上得到
`44.2% +/- 0.9%`，各类别的优势和失败均已公开。

数据集、指标、运行命令和当前限制见[评测说明](docs/evaluation.zh-CN.md)与 [Benchmark 指南](tests/benchmark/README.md)。

## 可选检查平台

默认交付物仍然是独立 C++ 引擎。[`platform/`](platform/README.md) 下的可选平台提供：

- FastAPI 文档上传和 Run API。
- Redis Streams 任务投递。
- 带 Stage Event 的常驻 C++ Worker。
- PostgreSQL Run 元数据。
- 用于选择 Backend 和检查 Artifact 的 React 界面。

Worker 会发出 `run_configured` 和 `stage_warning` 事件，记录每次运行请求与实际采用的 Backend、模型路径与
Profile，以及运行时 fallback 诊断。

```bash
cmake --preset platform-release
cmake --build --preset platform-release --target document_intelligence_worker --parallel
docker compose -f platform/deploy/docker-compose.yml up --build
```

平台同样处于早期阶段。Pending Job 恢复、严格超时、任务取消和重试原子发布仍属于路线图工作。

## 当前状态

端到端 Pipeline 已经运行。当前重点是：

1. 稳定、版本化、可追溯的文档契约。
2. 可恢复、幂等的 Worker 执行。
3. 面向代表性技术文档的端到端评测。
4. OCR、Reading Order、Document Assembly 和带来源引用的 RAG 输出。
5. C++ SDK API 稳定、Package 可移植性和更多嵌入示例。

更多输入格式、继续增加同类模型、多租户 SaaS 和大规模编排暂时不是当前优先级。

## 开始使用与相关资源

- [Roadmap](docs/roadmap.md)
- [依赖说明](docs/dependencies.md)
- [评测说明](docs/evaluation.zh-CN.md)
- [Document Contract v1](docs/document-contract-v1.zh-CN.md)
- [Quality Report v1](schemas/quality-report.v1.schema.json)
- [文本模型](docs/text-model.md)
- [可选平台](platform/README.md)
- [贡献指南](docs/community/contributing.zh-CN.md)
- [GitHub Issues](https://github.com/ChNanAn/technical-doc-parser/issues)

欢迎参与贡献。相比大范围重写或没有评测的新 Backend，项目更欢迎范围清晰、带可复现 Fixture、测试和指标证据的改进。

## License

本项目使用 [MIT License](LICENSE)。
