# 开源项目发布与招募资料包

仓库：<https://github.com/ChNanAn/technical-doc-parser>

这份资料用于发布 `v0.1.0` 和招募第一批贡献者。对外内容必须同时说明当前能力、公开证据和已知限制，
不要把 Schema 合法、回归测试通过或小型语料成绩包装成生产准确率。

配套资料：[维护者执行手册](maintainer-playbook.zh-CN.md)、
[第一批贡献任务](first-issues.zh-CN.md)。

## 统一定位

一句话：

> Document Intelligence Engine 是一个 C++ 原生、Backend 可替换、契约稳定且质量可评测的开源文档智能引擎。

差异化描述：

> 它不只是 PDF 转 Markdown 或模型调用封装，而是在 PDF 渲染、原生文本/OCR、Layout、Table、Reading
> Order、结构化输出和嵌入式部署之间建立长期可维护的 C++ Pipeline、Document Contract 和 SDK 边界。

英文简介：

> A C++-native, backend-agnostic document intelligence engine for traceable structured parsing, starting with
> technical and table-heavy PDFs.

不要直接宣传为“RAG 已完成”。更准确的说法是：

> 输出保留页码、bbox、Block、Table 和来源字段，为 source-grounded RAG 提供结构基础；RAG Chunk Contract、
> 引用一致率和检索评测仍在建设。

## 当前可以证明的能力

- PDFium 渲染和原生文本提取。
- PaddleOCR/Tesseract OCR，DocLayNet/Paddle Layout 和 Table Transformer Backend。
- Reading Order、Document Assembly、JSON/Markdown/HTML 输出。
- 宽松但可解释的 Document Contract v1 RC、JSON Schema、真实 complete/partial 输出校验。
- 可复用模型 Session 的 C++ `DocumentEngine` SDK 和稳定 C ABI v1。
- 确定性源码/CLI 发布包、独立模型包、GHCR 容器、SHA256 和许可证清单。
- Backend、Pipeline、Product 三层评测及可复现 Quality Report。
- 独立 1,403 页 olmOCR-Bench 首次结果：`44.2% +/- 0.9%`，8,413 条测试全部公开。

必须同时说明：

- 当前首个输入 Backend 是 PDF，不等于 Document Contract 永久限制为 PDF。
- `44.2%` 不是竞争领先成绩，而是可复现的公开起点。
- 多栏阅读顺序为 `52.3%`，旧扫描件为 `16.3%`，数学公式结构为 `0%`。
- 输入和页面渲染仍依赖磁盘，长推理暂时不能中途取消。
- Java/JNI 是 C ABI 之后的首个语言绑定方向，目前尚未发布。

## 30 秒介绍

> 我在维护一个 C++ 原生的开源文档智能引擎。现在已经跑通 PDF 渲染、OCR、版面、表格、阅读顺序和
> JSON/Markdown/HTML 输出，并把 Document Contract、C++ SDK、C ABI、发布包和三层质量评测做成了可验证
> 的工程边界。项目第一轮公开 olmOCR-Bench 是 44.2%，强项和失败类别都原样公开。下一步主要攻多栏阅读
> 顺序、页面旋转、旧扫描件、Java/JNI 和来源可追溯输出，欢迎从有 Fixture 和指标的 Issue 开始参与。

## 中文发布文案

标题：

> 我发布了一个 C++ 原生文档智能引擎：契约、SDK、C ABI 和真实基准都公开

正文：

> Document Intelligence Engine `v0.1.0` 是一个面向技术文档和复杂表格 PDF 的 C++17 文档解析引擎。
> 它把 PDFium、OCR、Layout、Table、Reading Order 和 Document Assembly 放在可替换 Backend 后面，
> 输出版本化 JSON 以及 Markdown/HTML。
>
> 这次发布重点不是再增加一个模型，而是让结果和工程边界变得可信：Document Contract v1 RC 有 Schema、
> Fixtures 和真实引擎校验；`DocumentEngine` 可以一次初始化、多次解析；C ABI 有固定符号、所有权和错误
> 模型；程序、模型、License 和 SHA256 分开发布。
>
> 我也公开了第一轮 1,403 页 olmOCR-Bench：总分 `44.2% +/- 0.9%`。Header/Footer 已有 94.5%，但多栏
> 只有 52.3%，旧扫描件 16.3%，公式结构 0%。这些数字不漂亮，但它们给每个后续版本提供了不能靠感觉
> 替代的上升曲线。
>
> 接下来优先处理多栏阅读顺序、页面方向、扫描件预处理、Java/JNI、无盘输入与取消机制。仓库准备了范围
> 明确的 Issue 草稿，欢迎 C++、OCR/CV、Java、评测和发布工程方向的贡献者参与。
>
> GitHub：<https://github.com/ChNanAn/technical-doc-parser>

## 短消息版本

> 发布一个 C++ 原生文档智能引擎 `v0.1.0`：PDF/OCR/Layout/Table/Reading Order 已跑通，Document
> Contract、C++ SDK、C ABI、独立模型包和三层评测已落地。首次 1,403 页 olmOCR-Bench 为 44.2%，失败
> 类别也全部公开。现在招募多栏/旋转/旧扫描件、Java/JNI、source-grounded RAG 和发布工程贡献者：
> https://github.com/ChNanAn/technical-doc-parser

英文标题：

> Show HN: A measurable C++ document intelligence engine with a stable C ABI

英文短文：

> I am releasing `v0.1.0` of Document Intelligence Engine, an early C++17 project for backend-agnostic,
> traceable document parsing. It includes a versioned Document Contract RC, a reusable C++ SDK, C ABI v1,
> deterministic Linux CLI and model packages, and three-layer quality evaluation. The first public 1,403-page
> olmOCR-Bench run scores `44.2% +/- 0.9%`; multi-column order, old scans, and math remain explicit weaknesses.
> Contributors interested in C++, OCR/CV, Java/JNI, evaluation, and supply-chain packaging are welcome.

## 渠道与角度

| 渠道 | 重点 | 主要证据 |
| --- | --- | --- |
| 知乎、掘金 | 为什么先做契约、SDK 和评测 | Contract、C ABI、Quality Report |
| V2EX、开源中国 | 可运行版本和招募 | CLI bundle、容器、第一批 Issue |
| Reddit r/cpp | C++ 生命周期和 ABI | Session 复用、Status、15 个导出符号 |
| Java 社区 | JNI 基础设施 | C ABI v1、所有权、异常防火墙 |
| OCR/CV 社区 | 可复现质量攻坚 | 多栏 52.3%、旧扫描 16.3% |
| RAG 社区 | 结构和引用边界 | page/bbox/source refs 与待建 Chunk Contract |

## 发布前检查

- `main` CI、release 配置测试和代码格式全部通过。
- `CHANGELOG.md`、Release Notes 和已知限制一致。
- CLI 与模型包在干净目录完成一次解压运行。
- Release 中同时发布源码、Linux CLI、模型包和 `SHA256SUMS`。
- GHCR tag 与 Git tag、CMake version 一致。
- 先创建 6 个描述完整的 Issue，并配置 `good first issue`、`help wanted` 和 area 标签。
- GitHub Discussions 至少建立“使用问题”“设计讨论”“展示案例”三个分类。
- 演示只使用仓库内公开 Fixture，不使用客户或公司文档。

发布内容最后只保留一个主要行动：

> 请从带 `good first issue` 或 `help wanted` 的任务开始；不确定范围时，先在 Discussions 描述你的方向和
> 可投入时间。
