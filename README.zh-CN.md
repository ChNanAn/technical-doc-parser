# Document Intelligence Engine

[English](README.md)

一个 C++ 原生、后端无关的文档智能引擎，面向结构化文档解析，当前优先处理技术文档和表格密集型 PDF。

项目通过类型化、可替换的 Backend 接口组合原生文本提取、OCR、版面分析、阅读顺序、表格结构识别和文档组装，输出 JSON、Markdown、HTML、页面图像及可选调试产物，供下游业务系统使用。

> 这是一个已经能够运行和评测的早期引擎，还不是成熟的企业级文档产品。当前公开 Benchmark 主要用于回归保护，不应被理解为生产准确率结论。

## 为什么做这个项目

- **C++ 原生**：面向离线、私有化和嵌入式部署。
- **后端无关**：PDF、OCR、Layout、Table 提供方可以演进，不需要重写 Pipeline。
- **结构化输出**：文本、版面块、表格、阅读顺序、页码、bbox 和置信度进入统一类型模型。
- **可检查、可评测**：中间产物、公开 Fixture、指标、冒烟测试和 CI 都是引擎的一部分。
- **面向 SDK 演进**：当前提供 CLI，稳定 C++ Library/SDK facade 仍在路线图中。

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

解析文档：

```bash
./build/core-release/cpp/app/document_intelligence_engine input.pdf --out output/
```

可以显式选择 Backend，也可以使用版本化 Registry 配置：

```bash
./build/core-release/cpp/app/document_intelligence_engine input.pdf --out output/ \
  --ocr-backend auto \
  --layout-backend auto \
  --table-backend auto \
  --backend-config config/backends.json
```

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

普通 JSON 输出包含组装后的文档块和页面产物。启用 `--debug` 后，还会包含归一化文本、Layout Blocks、Reading Order、表格结构和图像预处理产物。

```json
{
  "source": {"path": "input.pdf", "type": "pdf"},
  "render": {"dpi": 200},
  "blocks": [
    {
      "id": "doc_page_1_block_1",
      "type": "paragraph",
      "page_number": 1,
      "bbox": {"x0": 84.0, "y0": 132.0, "x1": 742.0, "y1": 168.0},
      "confidence": 0.92,
      "text": "Technical specification"
    }
  ]
}
```

版本化公共文档契约、完整 Backend provenance 和带来源引用的 RAG Chunk Schema 正在为第一个稳定 API 版本设计。

## 评测

仓库包含可再分发的 OCR、Layout 和 Table Fixture，以及与具体 Backend 无关的 Evaluator。完整模型构建包含真实 PaddleOCR、DocLayNet、Paddle Layout 和 Table Transformer 推理回归。

已提交的模型评测集规模有意保持较小，主要用于防止预处理、推理、标签映射和后处理发生回退；项目仍需要更广泛的技术文档验证。

数据集、指标、运行命令和当前限制见[评测说明](docs/evaluation.md)与 [Benchmark 指南](tests/benchmark/README.md)。

## 可选检查平台

默认交付物仍然是独立 C++ 引擎。[`platform/`](platform/README.md) 下的可选平台提供：

- FastAPI 文档上传和 Run API。
- Redis Streams 任务投递。
- 带 Stage Event 的常驻 C++ Worker。
- PostgreSQL Run 元数据。
- 用于选择 Backend 和检查 Artifact 的 React 界面。

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
5. 可复用的 `DocumentEngine` C++ SDK facade 和模型 Session 复用。

更多输入格式、继续增加同类模型、多租户 SaaS 和大规模编排暂时不是当前优先级。

## 开始使用与相关资源

- [Roadmap](docs/roadmap.md)
- [依赖说明](docs/dependencies.md)
- [评测说明](docs/evaluation.md)
- [文本模型](docs/text-model.md)
- [可选平台](platform/README.md)
- [贡献指南](docs/community/contributing.zh-CN.md)
- [GitHub Issues](https://github.com/ChNanAn/technical-doc-parser/issues)

欢迎参与贡献。相比大范围重写或没有评测的新 Backend，项目更欢迎范围清晰、带可复现 Fixture、测试和指标证据的改进。

## License

本项目使用 [MIT License](LICENSE)。
