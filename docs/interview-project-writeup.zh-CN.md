# Technical Doc Parser 项目简历与面试讲解稿

## 1. 项目一句话定位

这个项目是一个 **C++ 原生、后端可替换、可评测、可嵌入部署的文档智能解析引擎**，主要面向技术文档和表格密集型 PDF。它不是单纯做 OCR，而是把 PDF 渲染、原生文本提取、OCR、版面分析、表格结构识别、阅读顺序恢复、文档组装和多格式导出串成一个完整 Pipeline，最终输出带页码、bbox、表格结构、阅读顺序、置信度和来源引用的结构化文档结果。

面试时可以先用这句话开场：

> 我做的是一个面向私有化和离线部署的 C++ 文档智能引擎，核心不是简单 PDF 转文本，而是把不稳定的 OCR/Layout/Table 模型能力，封装成可替换、可追踪、可评测、可嵌入的工程系统。

## 2. 简历写法

### 2.1 详细版

**Document Intelligence Engine｜C++ 原生技术文档解析引擎**

技术栈：C++17、CMake、PDFium、ONNX Runtime、OpenCV、PaddleOCR、DocLayNet、Paddle PP-DocLayoutV3、Table Transformer、Tesseract、nlohmann-json、spdlog、FastAPI、Redis Streams、PostgreSQL、React、Docker、GitHub Actions

- 设计并实现面向技术文档和表格密集型 PDF 的端到端解析 Pipeline，覆盖 PDF 渲染、原生文本/OCR、版面分析、表格结构识别、阅读顺序恢复、文档组装和 JSON/Markdown/HTML 导出。
- 抽象 OCR、Layout、Table、Document Source 等可替换 Backend 接口，接入 PaddleOCR ONNX、Tesseract、RF-DETR DocLayNet、Paddle PP-DocLayoutV3、Table Transformer，并支持自动后端选择、fallback、warning 和运行 provenance 追踪。
- 设计统一 Document Contract v1，将文本、bbox、页码、表格、阅读顺序、置信度和来源引用归一化，保证下游系统可以稳定消费解析结果。
- 提供可嵌入 C++ SDK 与 C ABI，支持 `DocumentEngine` 复用模型 Session，降低多次解析时的初始化成本，并为后续 Java/JNI 集成预留稳定 ABI 边界。
- 建立 Backend、Pipeline、Product 三层质量评测思路，已实现 OCR CER/WER、Layout F1、Table Structure F1、Table Text CER、文本完整率、重复率、Reading Order 等回归指标，并接入 CI 防止模型前后处理和 Pipeline 回退。
- 完成 CLI、Docker、模型包 SHA256 校验、portable Linux bundle 和可选检查平台；平台包含 FastAPI Run API、Redis Streams Worker、PostgreSQL 元数据和 React Artifact Inspector。

### 2.2 精简版

- 主导开发 C++17 文档智能引擎，将技术/表格密集型 PDF 解析为带 bbox、阅读顺序、表格结构和来源引用的结构化 JSON/Markdown/HTML。
- 设计 Backend Registry 与分阶段 Pipeline，接入 PDFium、PaddleOCR ONNX、DocLayNet、PP-DocLayoutV3、Table Transformer 等后端，支持自动选择、fallback 与可追踪 warning。
- 封装可复用 `DocumentEngine`、C++ SDK 和 C ABI，复用模型 Session，并将核心引擎与 CLI、Worker、导出格式解耦。
- 建立 OCR/Layout/Table/Pipeline 多层评测与 GitHub Actions 回归门禁，支持 Docker/portable CLI/模型包独立发布等工程化交付。

## 3. 项目背景和为什么做

很多业务场景需要处理 PDF 技术资料，例如说明书、标准文档、实验报告、财务表格、表单、规格书等。单纯把 PDF 转成纯文本通常不够，因为下游系统真正需要的是：

- 哪些内容是标题、段落、表格、图片、页眉页脚；
- 每个内容块来自哪一页、页面上的哪个位置；
- 多栏文档应该按什么顺序阅读；
- 表格的行列、单元格、表头和跨行跨列关系是什么；
- 后续做搜索、RAG 或审计时，答案能不能追溯回原始页面区域。

所以这个项目解决的问题不是“识别文字”，而是 **把非结构化 PDF 转成可定位、可追溯、可验证的结构化文档模型**。

项目选择 C++ 原生实现，主要考虑：

- 私有化和离线部署场景更容易接受 native engine；
- 可以作为 SDK 嵌入其他系统；
- 对 PDF 渲染、图像预处理、ONNX 推理和内存控制有更强掌控；
- 避免把核心能力绑定在某个 Python 框架或某个模型服务上。

## 4. 整体架构

核心 Pipeline 是：

```text
Document
  -> Render
  -> Native Text / OCR
  -> Layout Analysis
  -> Table Structure Recognition
  -> Reading Order
  -> Document Assembly
  -> JSON / Markdown / HTML Export
```

仓库里的核心模块大致对应：

- `cpp/document_source/`：PDF 打开、渲染、原生文本提取，主要基于 PDFium。
- `cpp/ocr/`：OCR 后端接口和 PaddleOCR ONNX、Tesseract、Noop 等实现。
- `cpp/layout/`：版面分析后端接口和 DocLayNet、Paddle Layout、Text Layout fallback。
- `cpp/table/`：表格结构识别后端接口和 Table Transformer、Text Table fallback。
- `cpp/reading_order/`：阅读顺序恢复，例如 docling-like baseline。
- `cpp/assembly/`：把页面级中间产物组装成统一 `ParsedDocument`。
- `cpp/export/`：导出 JSON、Markdown、HTML。
- `cpp/pipeline/`：Pipeline 编排、Backend Registry、EngineConfig、StageObserver、RunProvenance。
- `sdk/cpp/` 和 `sdk/c/`：C++ SDK 与 C ABI。
- `platform/`：可选平台，包含 FastAPI、Redis Streams Worker、PostgreSQL、React 检查界面。

## 5. 核心设计考虑

### 5.1 不把项目绑定到单个模型

OCR、Layout、Table 这些能力都会快速变化。如果代码直接依赖某个模型的输入输出格式，后续替换模型会非常痛苦。

所以项目里把模型都做成 Backend：

- OCR 后端只负责输出统一的文本行和 bbox；
- Layout 后端只负责输出统一的版面块；
- Table 后端只负责输出统一的表格结构；
- Pipeline 只依赖抽象接口，不关心底层是 PaddleOCR、Tesseract、DocLayNet 还是 Table Transformer。

这样后续如果想换更好的 OCR 或 Layout 模型，只需要新增 adapter，不需要重写完整 Pipeline。

### 5.2 输出契约比模型输出更稳定

AI 模型经常升级，模型标签体系也不统一。比如 DocLayNet 和 Paddle Layout 的类别不完全一致，表格模型也有自己的对象定义。

项目选择把长期稳定边界放在 Document Contract v1 上：

- `pages` 表示页面信息；
- `blocks` 表示最终阅读顺序下的标题、段落、表格、图片等；
- 每个 block 可以带 `page_id`、`bbox`、`confidence`、`source_refs`；
- 表格 block 内部可以带 rows/cells；
- `warnings` 表示可用但有降级的结果；
- `producer` 记录版本、运行 ID、git revision 等追踪信息。

这样下游系统不需要知道 OCR 和 Layout 具体用了哪个模型，只需要消费稳定的文档契约。

### 5.3 区分 hard failure 和 partial result

文档解析里有些错误是硬失败，比如文件打不开、PDF 渲染失败、输出目录不可写；但有些错误不应该让整份文档失败，比如某个 Layout 后端不可用，可以 fallback 到 text layout。

所以项目里设计了：

- `Status`：表示硬失败，包含 code、message、stage、retryable；
- `Diagnostic` / `Warning`：表示可恢复问题；
- `document.status = partial`：表示输出可用，但发生过降级；
- `RunProvenance`：记录实际解析时使用了哪些 backend、发生了哪些 fallback。

这个设计的好处是下游可以根据业务需求决定：是接受 partial result，还是要求 complete result。

### 5.4 可检查和可评测是 Pipeline 的一部分

文档智能类项目很容易陷入“看 demo 觉得还行”的状态，但真实质量很难保证。所以项目一开始就把 debug artifact、fixture、指标和 CI 作为工程能力来做。

启用 debug 时，输出里会带中间产物：

- 原始/归一化文本；
- Layout blocks；
- Table structures；
- Reading order；
- 图像预处理结果；
- warning 和 fallback 信息。

这些信息可以帮助定位到底是 OCR 错了、Layout 错了、Table 错了，还是 Assembly 阶段把正确的中间结果组装错了。

## 6. 具体怎么实现

### 6.1 Pipeline 执行流程

一次解析大致是：

1. 根据 `EngineConfig` 创建 `BackendRegistry`。
2. 通过 registry 解析用户请求的 backend，例如 `auto`、`paddle`、`doclaynet`、`table-transformer`。
3. 打开 PDF，读取页数，并计算源文件 size 和 SHA256。
4. 用 PDFium 渲染页面图像，按 DPI 输出 page image artifact。
5. 文本阶段优先拿 PDF 原生文本，如果不够可用，再调用 OCR 后端。
6. Layout 阶段调用版面模型，失败时可 fallback 到文本版面规则。
7. Table 阶段在表格区域上做结构识别和 cell 文本分配。
8. Reading Order 阶段根据版面块位置、列布局、caption 等规则恢复阅读顺序。
9. Assembly 阶段把 page artifact、text、layout、table、reading order 合并成最终 `ParsedDocument`。
10. Export 阶段写出 `document.json`、`document.md`、`document.html` 和页面图像。

面试里可以强调：每个 stage 都不是直接写文件给下一个 stage，而是通过类型化结构传递，最后由 Assembly 统一生成文档模型。

### 6.2 Backend Registry

`BackendRegistry` 负责管理不同后端的创建：

- document：`pdf`
- ocr：`noop`、`tesseract`、`paddle`
- layout：`text`、`doclaynet`、`paddle-layout`
- table：`text`、`table-transformer`

`config/backends.json` 里可以配置 `auto_order`，例如 layout 优先用 `doclaynet`，失败再用 `paddle-layout`，最后 fallback 到 `text`。这样“自动选择策略”不是写死在代码里，而是变成版本化配置。

### 6.3 DocumentEngine 和 SDK

CLI 可以每次执行解析，但真实业务通常是一个服务进程里解析很多文件。如果每次都重新加载模型，初始化成本会很高。

所以项目提供 `DocumentEngine`：

- 初始化时创建并持有 backend/model session；
- 多次 `parse()` 复用同一组模型；
- 一个 engine 顺序处理一个 parse；
- 并发时可以创建多个 engine；
- `ParseResult` 返回 `status`、`document`、`artifacts`、`provenance`。

同时还提供 C ABI：

- 不暴露 C++ STL 类型；
- 不让 C++ exception 穿过 ABI；
- 使用 opaque handle 管理 engine/document/error；
- 配置和结果用 JSON 作为边界；
- 为 Java/JNI binding 预留稳定接口。

### 6.4 可选平台

核心产品仍然是 C++ 引擎和 CLI，`platform/` 是可选检查平台。平台链路是：

```text
Browser
  -> FastAPI
  -> PostgreSQL metadata
  -> Redis Streams job
  -> persistent C++ Worker
  -> shared artifact storage
  -> React Inspector
```

这个平台的作用不是替代核心引擎，而是方便上传文档、选择 backend、观察 stage event、查看 artifact 和 debug 信息。

C++ Worker 通过 `StageObserver` 接收核心 Pipeline 的阶段事件，然后发布成平台协议里的 event，例如：

- run configured；
- stage started；
- stage progress；
- stage warning；
- stage completed；
- artifact ready；
- job succeeded / failed。

这样核心引擎不需要知道 FastAPI、Redis、PostgreSQL 或 React 的存在，平台只是观察和编排层。

## 7. 评测体系怎么做

项目把质量拆成三层：

```text
Backend 层：单个模型/Provider 本职能力如何
Pipeline 层：多个 Stage 组合后，最终文档内容是否保住并组织正确
Product 层：下游拿到的交付结果是否可靠、稳定、可消费
```

### 7.1 Backend 层

Backend 层的原则是：只看单点能力，不混入完整 Pipeline。

#### OCR CER / WER

OCR 后端会先输出统一 prediction JSON：

```json
{
  "version": 1,
  "task": "ocr_text",
  "samples": [
    {
      "id": "eurotext",
      "text": "recognized text"
    }
  ]
}
```

独立 Python evaluator 做归一化：

- Unicode NFKC；
- 空白折叠；
- 大小写归一；
- 缺失样本按空预测计分；
- 重复 ID、未知 ID、未知 dataset 直接报错。

指标公式：

```text
CER = 字符级 Levenshtein 编辑距离 / 参考字符数
WER = 词级 Levenshtein 编辑距离 / 参考词数
```

这样可以避免“失败样本被过滤掉以后分数变好”的问题。

#### Layout F1

Layout 后端输出统一对象：

```json
{
  "label": "table",
  "bbox": [x0, y0, x1, y1],
  "confidence": 0.9
}
```

evaluator 按类别和 IoU 做一对一匹配。默认 IoU 阈值是 `0.5`。

指标包括：

- precision；
- recall；
- micro-F1；
- macro-F1；
- per-class F1；
- mean matched IoU。

这样可以区分整体效果下降，还是某个类别退化，例如 table、figure、header/footer。

#### Table Structure F1 和 Table Text CER

表格评测拆成两部分：

1. 结构是否识别对：table、row、column、header、spanning cell 等对象；
2. 结构匹配之后，cell 里的文字是否正确。

结构对象按类别和 bbox IoU 做一对一匹配，得到 Table Structure F1。

Cell 文本则先匹配 cell，再计算：

```text
Table Text CER = 匹配 cell 文本的字符编辑距离 / 参考 cell 字符数
```

这样能区分三类问题：

- 表格框没找到；
- 表格结构找到了，但行列/跨列错了；
- 结构对了，但 OCR 或 cell text assignment 错了。

### 7.2 Pipeline 层

Pipeline 层评估完整链路最终产物，也就是最终 `DocumentBlock`，不直接测中间模型。

执行流程：

```text
固定质量语料
  -> C++ pipeline_quality_eval 复用一个 DocumentEngine 跑完整解析
  -> 输出最终 blocks：type、bbox、text、page
  -> Python evaluate_pipeline.py 对最终结果打分
  -> 生成 pipeline_quality_report.json
  -> generate_quality_report.py 转成 Quality Report v1
  -> CI 根据 profile 里的阈值判断是否回退
```

#### 文本完整率

语料里有人工复核的 reference anchors。evaluator 会把每个 anchor 的文本和最终输出 block 做对齐。

公式：

```text
text_completeness = 匹配到的参考 anchor 字符数 / 总参考 anchor 字符数
```

它不是全文召回率，而是对关键文本 span 的采样完整率。这样做的原因是人工完整标注所有页面成本太高，但可以先把关键内容做成稳定回归集。

#### Reading Order

每个页面的 ground truth 里有明确的 anchor 顺序。evaluator 先判断 anchor 是否被匹配到，只有匹配成功的 anchor 才参与顺序比较。

公式：

```text
reading_order_anchor_recall = 匹配到的 anchor 数 / 总 anchor 数
reading_order_score = 顺序正确的 anchor pair 数 / 可比较 anchor pair 数
```

举例：参考顺序是 A -> B -> C，如果 A、B、C 都被匹配到，就比较 AB、AC、BC 三对在最终输出中的相对顺序。

这里单独统计 `anchor_recall` 很重要，因为如果只看顺序分数，模型漏掉很多 anchor 后，剩下少数 anchor 的顺序可能看起来很高。

#### 文本重复率

重复率用完整全文参考计算，目前只覆盖有独立全文参考的页面。

公式：

```text
text_duplication_rate = 输出中超出参考字符多重集的字符数 / 输出字符数
```

它不关心阅读顺序，只看输出里是否把同一批字符重复抽取。这样可以发现原生文本和 OCR 混合时常见的问题：同一段内容既从 PDF text layer 来了一遍，又从 OCR 来了一遍。

同时报告 `full_text_reference_coverage`，明确说明只有多少页有全文参考，避免把部分参考包装成完整全量结论。

#### 当前 Pipeline 回归门槛

当前固定质量基线主要用于防止回退，不代表生产准确率：

```text
text_completeness >= 0.88
reading_order_anchor_recall >= 0.84
reading_order_score >= 0.92
text_duplication_rate <= 0.16
```

这些阈值的作用是：如果后续改 OCR、Layout、Table 或 Assembly，只要把已有固定语料改坏，CI 就会失败。

### 7.3 Product 层

Product 层关注的是下游拿到的交付物是否可靠。

已实现和验证的部分：

- `Document Contract v1` JSON Schema；
- contract example 校验；
- reviewed snapshots 校验；
- bbox 必须落在 page 内；
- block 的 `page_id`、source reference、relation、warning 引用必须可解析；
- `partial` result 必须带 warning；
- CLI smoke test；
- C API smoke test；
- platform protocol schema test；
- release packaging 和 Linux CLI ABI 兼容性检查。

规划中的完整 Product 指标包括：

```text
document_success_rate
json_contract_valid_rate
rag_citation_completeness
latency_ms_per_page_p50/p95
peak_rss_mib_p50/p95
```

面试时要诚实区分：

> Product 层我已经先把输出契约、schema、snapshot、CLI、C API 和打包边界做了；完整的成功率、延迟和内存统计已经在评测设计里定义，下一步是把它们接入统一 end-to-end runner 和 Quality Report v1。

### 7.4 Quality Report v1

为了让评测结果可比较，项目定义了 `quality-report.v1.schema.json`。

每份质量报告记录：

- subject：引擎名称、版本、git revision、配置 ID；
- corpus：语料名称、版本、split、样本量、manifest hash；
- layers：backend、pipeline、product；
- metrics：指标名称、值、单位、方向、分子分母、阈值；
- artifacts：关联预测文件和报告文件。

Pipeline 评测先生成 evaluator 自己的报告，再通过 profile 转成统一 Quality Report：

```text
pipeline_quality_report.json
  -> pipeline-quality-v1 profile
  -> pipeline_quality_report.v1.json
  -> schema validate
```

profile 使用 JSON Pointer 显式指定每个指标从哪里取值。这样新增指标不会悄悄改变旧指标语义。

## 8. 外部 benchmark 怎么做

项目还跑过外部 `olmOCR-Bench`。

做法是：

1. 下载独立外部 corpus；
2. 用项目 engine 跑完整解析；
3. 把输出转换成 benchmark 要求的 candidate 格式；
4. 使用官方 scorer 评分；
5. 解析失败的样本输出空结果，而不是删除样本；
6. 记录总分、分类分数、命令、模型配置、硬件环境、失败类别和限制。

这个 benchmark 的定位是 baseline，不是宣传生产准确率。

面试可以这样讲：

> 我没有把外部 benchmark 分数包装成生产准确率，而是作为独立验证基线。第一次完整跑 1,403 页，记录总分和分类失败，比如 old scans、多栏顺序、表格、公式等。后续每次优化 OCR、Reading Order、Table 或公式导出，都可以沿同一套 benchmark 对比，避免只挑 demo。

## 9. 遇到的问题和解决方案

### 9.1 原生文本和 OCR 容易重复

问题：

有些 PDF 同时有 text layer 和可渲染图像。如果同时使用原生文本和 OCR，容易把同一段文字输出两遍。

解决：

- 在文本阶段区分 `pdf_text_layer`、`ocr`、`mixed` 等来源；
- Pipeline 层增加文本重复率指标；
- 重复率使用字符多重集统计，不受阅读顺序影响；
- Assembly 阶段保留 source refs，方便定位重复来自哪个来源。

面试表达：

> 我没有只靠肉眼看结果，而是专门做了 duplication rate。它能发现 text layer 和 OCR 合并时重复输出的问题，而且和 reading order 解耦。

### 9.2 多栏文档阅读顺序容易错

问题：

多栏技术文档、目录页和扫描资料里，按 y 坐标简单排序会把左右栏交错拼接，导致最终文本顺序错误。

解决：

- 单独设计 Reading Order stage，而不是把顺序逻辑散落在 Assembly 里；
- 使用 docling-like baseline 处理列布局、caption 和空间关系；
- Pipeline 评测里引入 anchor pair 的 reading order score；
- 同时统计 anchor recall，防止漏掉内容后顺序分数虚高。

面试表达：

> 阅读顺序不是用整页字符串相等来测，而是把人工复核 anchor 的相对顺序转成 pairwise score，这样能更稳定地发现多栏顺序问题。

### 9.3 模型后端不可用或效果不稳定

问题：

ONNX Runtime、模型文件、PDFium、OpenCV 这些依赖在不同机器上可能缺失；模型推理也可能因输入异常失败。如果一个模型失败就导致整份文档失败，系统可用性会很差。

解决：

- Backend 通过 registry 管理；
- `auto_order` 用版本化配置控制 fallback 顺序；
- hard failure 用 `Status` 返回；
- 可恢复问题用 warning 和 `document.status = partial` 表示；
- provenance 记录 failed backend、fallback backend 和原因；
- CI 里测试 no-onnx、no-pdfium、vcpkg、release packaging 等构建边界。

面试表达：

> 我把失败分成硬失败和可恢复降级。比如 Layout 模型不可用时可以 fallback 到 text layout，最终结果标记 partial，并把 warning 和 fallback provenance 写进结果里。

### 9.4 不同模型标签体系不一致

问题：

DocLayNet、Paddle Layout、Table Transformer 的标签体系不一样。例如同样是正文、列表、表格、图片，不同模型类别名和粒度都不同。

解决：

- 在 backend adapter 里做 label mapping；
- Pipeline 内部只使用统一 `LayoutBlockType` 和 `DocumentBlockType`;
- evaluator 也只消费统一后的 mapped label；
- 评测报告保留 per-class 明细，方便发现映射导致的类别损失。

面试表达：

> 模型输出不是直接进入最终 JSON，而是先映射到内部统一类型。这样下游 contract 不随模型 taxonomy 改变。

### 9.5 表格不能只看检测框

问题：

表格密集型文档里，只检测出 table bbox 不够。下游更关心行列、表头、跨行跨列和单元格文本。

解决：

- Table stage 先根据 layout 找表格区域；
- 对表格区域做结构识别；
- 将 OCR 文本分配到 cell；
- DocumentBlock 里把 table rows/cells 嵌入最终输出；
- 评测拆成 Table Structure F1 和 Table Text CER。

面试表达：

> 我把表格质量拆成结构和文字两层。结构对不代表 cell 文本对，所以匹配 cell 之后还要算 table text CER。

### 9.6 输出 JSON 通过 schema 不等于语义正确

问题：

JSON Schema 能检查字段类型，但不能完全检查引用语义。例如 bbox 是否超出页面、source_ref 是否引用存在页面、relation 是否指向存在 block。

解决：

- 定义 Document Contract v1 schema；
- 增加 Python semantic validator；
- snapshot 覆盖 native text、scanned OCR、complex table、multi-column 等典型场景；
- partial result 必须带 warning；
- source 文件 size 和 SHA256 写入结果，保证来源可追踪。

面试表达：

> 我没有只做 schema validate，还加了语义校验，比如 bbox 必须落在页面内，source_ref 必须能解析到 page，partial 必须有 warning。

### 9.7 C++ SDK 要避免 ABI 风险

问题：

C++ 类型、STL、异常和编译器 ABI 在跨语言绑定时都容易出问题。后续如果做 Java/JNI，不能直接暴露 C++ 类。

解决：

- 对 C++ 用户提供 typed SDK；
- 对跨语言用户提供 C ABI；
- C ABI 使用 opaque handles；
- 错误通过 `die_error_t` 返回；
- 配置和结果都使用 JSON；
- ABI 版本和 Document Contract 版本分开管理。

面试表达：

> C++ SDK 面向 native 调用，C ABI 面向跨语言边界。C ABI 不暴露 STL、不抛异常，生命周期由 opaque handle 管理。

### 9.8 模型和程序包耦合会影响部署

问题：

模型文件通常很大，而且许可证、版本和更新节奏与程序不同。如果把模型直接打进程序镜像，发布和升级都会很重。

解决：

- 程序包和模型包独立版本化；
- model manifest 记录 URL、SHA256、license、上游 revision；
- model-init 服务下载并校验模型；
- Worker 镜像保持 model-free；
- portable CLI 做 glibc 和 libstdc++ ABI 检查；
- release packaging 失败时阻止发布。

面试表达：

> 我把模型包和程序包拆开，程序镜像不内置模型，启动前由 model-init 校验 SHA256。这样程序升级和模型升级可以分开。

### 9.9 平台 Worker 不能重复加载模型

问题：

文档解析服务如果每个 job 都重新创建 engine，会反复加载模型，延迟和内存抖动都会很大。

解决：

- Worker 是常驻进程；
- `DocumentEngine` 可以复用模型 session；
- Worker 用 backend tuple 做 key，维护 bounded LRU engine cache；
- 单个 engine 顺序处理一个 parse；
- 多 Worker 容器实现横向并发。

面试表达：

> Worker 里不是每个任务都重新加载模型，而是按 backend 配置缓存 DocumentEngine，既减少冷启动成本，也避免非线程安全模型 session 被并发访问。

## 10. 面试讲解模板

### 10.1 一分钟版本

这个项目是一个 C++ 原生的文档智能解析引擎，主要处理技术文档和表格密集型 PDF。它不是简单 OCR，而是把 PDF 渲染、原生文本提取、OCR、版面分析、表格结构识别、阅读顺序恢复和文档组装串成一个可替换后端的 Pipeline，最终输出统一的结构化 Document JSON，同时也能导出 Markdown 和 HTML。

我在设计上比较重视工程化边界：模型只是 Backend，长期稳定的是统一文档模型、SDK、C ABI、评测体系和部署方式。为了避免模型效果不可控，我做了 Backend、Pipeline、Product 三层评测：Backend 看 OCR CER/WER、Layout F1、Table F1；Pipeline 看文本完整率、重复率和阅读顺序；Product 层先落地 JSON contract、schema、snapshot、CLI/C API smoke 和打包校验，后续再接入成功率、延迟和内存指标。

### 10.2 三分钟版本

项目背景是普通 PDF 转文本不能满足技术文档场景，因为下游需要标题、段落、表格、图片、页码、bbox、阅读顺序和来源引用。我把它拆成几个 stage：Render、Text/OCR、Layout、Table、Reading Order、Assembly、Export。每个 stage 输入输出都是类型化模型，OCR、Layout、Table 都通过 Backend 接口接入，所以可以替换 PaddleOCR、Tesseract、DocLayNet、Paddle Layout 或 Table Transformer，而不影响最终 Document Contract。

核心难点有三个。第一是模型输出不稳定，所以我用统一的 `ParsedDocument` 和 Document Contract v1 稳定下游边界。第二是失败不能简单崩溃，所以我区分 hard failure 和 partial result，用 `Status` 表示硬失败，用 warning/provenance 记录 fallback。第三是质量不能靠 demo 判断，所以我做了独立 evaluator 和 CI 回归门禁。Backend 层分别测 OCR、Layout、Table 的单点能力；Pipeline 层用最终 `DocumentBlock` 测文本完整率、重复率和阅读顺序；外部 benchmark 只作为 baseline，不包装成生产准确率。

工程交付上，项目有 CLI、C++ SDK、C ABI、Docker、portable CLI bundle 和可选检查平台。平台用 FastAPI 接收上传和创建 run，用 Redis Streams 投递任务，用常驻 C++ Worker 复用 `DocumentEngine`，React 页面查看 stage event 和 artifact。模型包和程序包独立版本化，并做 SHA256 校验，避免模型重量和许可证影响程序发布。

### 10.3 面试官追问“你最大的收获是什么”

可以这样回答：

> 最大的收获是文档智能项目不能只盯模型分数。真正工程上要解决的是边界稳定、失败可解释、结果可追溯和质量可复现。模型会变，但 Document Contract、Backend 抽象、Pipeline artifact、质量评测和部署边界要稳定。这个项目让我把 AI 能力从 demo 变成了一个可以被下游系统调用、验证和迭代的工程系统。

## 11. 可以主动承认的不足

面试时主动承认不足反而更可信：

- 当前仍是早期引擎，不是成熟企业级文档产品；
- 固定评测语料规模较小，主要用于回归保护，不代表生产准确率；
- old scans、多栏阅读顺序、复杂表格、跨页表格和公式结构化输出仍是薄弱点；
- Product 层的成功率、p50/p95 延迟、Peak RSS 还需要接入统一 end-to-end runner；
- 平台的 pending job 恢复、严格取消、重试原子发布还在路线图中。

推荐说法：

> 我不会把当前指标说成生产准确率。现在更准确的定位是：这个项目已经有可运行 Pipeline、可复现评测和工程化交付边界，评测集主要用于防止回退；后续要继续扩大行业语料，补齐 Product 层指标和更多复杂场景。

## 12. 面试中可能被问到的问题

### Q1：为什么不用 Python？

可以回答：

> Python 更适合快速验证模型，但这个项目目标是私有化、离线和嵌入式部署，所以核心引擎用 C++。C++ 对 PDF 渲染、图像预处理、ONNX 推理、内存和部署包控制更直接。Python 在项目里主要用在 evaluator、API 和辅助脚本上。

### Q2：为什么要做 Backend 抽象？

可以回答：

> 因为 OCR/Layout/Table 模型变化很快，如果 Pipeline 直接依赖某个模型格式，后续替换成本很高。Backend 抽象让模型只是 adapter，稳定边界是统一文档模型和 Pipeline stage contract。

### Q3：怎么判断是 OCR 错了还是 Layout 错了？

可以回答：

> 我把评测拆成 Backend 和 Pipeline 两层。Backend 单独测 OCR CER/WER、Layout F1、Table F1；Pipeline 再测最终文本完整率、重复率和阅读顺序。再结合 debug artifact，可以定位问题发生在哪个 stage。

### Q4：如果某个后端失败怎么办？

可以回答：

> 硬失败直接返回 `Status`，比如文件打不开或渲染失败。可恢复失败走 fallback，比如 Layout 模型不可用时 fallback 到 text layout，最终文档标记为 partial，并写入 warning 和 provenance。

### Q5：怎么保证输出能被下游稳定使用？

可以回答：

> 我定义了 Document Contract v1，用 JSON Schema 和语义测试保证字段结构、page 引用、bbox、source_refs、relations、warnings 都合法。模型怎么换不应该影响下游消费的契约。

### Q6：外部 benchmark 分数不高怎么办？

可以回答：

> 我会如实说这是 baseline，不是生产准确率。外部 benchmark 的价值是提供独立验证曲线，让后续优化可以比较，而不是只挑 demo。分数低的类别，比如 old scans、公式、多栏和复杂表格，也正好指导后续优化方向。

## 13. 从提交复盘具体代码设计问题和解决方案

这一节建议按“提交演进”来讲，不要上来就说用了什么设计模式。更可信的讲法是：当时代码遇到了什么问题，我提交里怎么改，改完解决了什么，再顺带说这个结构类似什么模式。

下面这些案例都来自仓库提交记录，可以在面试时按需挑 3 到 5 个讲。

### 13.1 后端和 Pipeline 边界混在一起

相关提交：

- `e9ecb56 refactor: introduce backend and exporter interfaces`
- `3bc64b5 refactor: clarify backend and pipeline factory boundaries`
- `3b40645 refactor(pipeline): create document sources as services`
- `cd03e4c refactor(pipeline): depend directly on backend interfaces`

当时的问题：

早期 Pipeline 里容易直接知道 PDFium、PDF backend、export writer 这些具体实现。这样短期能跑，但后续一加 OCR、Layout、Table 后端，Pipeline 会越来越像“上帝对象”：既创建后端，又调流程，又做导出。问题是模型/Provider 一变，Pipeline 主流程也要跟着变。

怎么改的：

- 把 document backend、OCR backend、exporter 等拆成接口。
- 把 PDFium backend 从 Pipeline 里挪出去，Pipeline 只依赖 backend interface。
- 把 exporter 从原来的 JSON writer 扩成 `IDocumentExporter` / `JsonDocumentExporter`。
- 把 service creation 和 pipeline run 分开，逐步形成后来的 `PipelineServices` / `pipeline_service_factory`。

实际改动文件包括：

- `cpp/pipeline/document_pipeline.cpp`
- `cpp/pipeline/pipeline_service_factory.cpp`
- `cpp/export/document_exporter.h`
- `cpp/export/json_document_exporter.cpp`
- 当时的 `cpp/backend/` / 后来的 `cpp/document_source/` 相关文件

解决结果：

Pipeline 开始只负责“编排阶段”，不负责“知道所有具体实现”。后续接入 PaddleOCR、DocLayNet、Table Transformer 时，只要实现 backend adapter，不需要改主流程。

可以顺带说的设计模式：接口隔离 + Strategy 思路。但面试里重点说“我先把边界拆出来”，不要硬背 Strategy。

面试说法：

> 最早的问题是 Pipeline 知道太多具体 backend，后续一接模型会越来越难维护。所以我通过几个 refactor 把 backend 和 exporter 接口抽出来，让 Pipeline 只依赖抽象能力，具体 PDFium、OCR、Layout、Table 都变成可替换实现。

### 13.2 `auto` 后端不能写死在 if-else 里

相关提交：

- `d54e6df feat(pipeline): add config-driven backend registry`

当时的问题：

项目里有多个 OCR/Layout/Table 后端，比如 OCR 有 PaddleOCR/Tesseract/noop，Layout 有 DocLayNet/Paddle Layout/text fallback，Table 有 Table Transformer/text fallback。如果用户传 `auto`，到底先用哪个、失败后用哪个，不能写死在 C++ if-else 里。因为不同部署环境模型文件可能不一样，后续也可能调整优先级。

怎么改的：

- 新增 `cpp/pipeline/backend_registry.h/.cpp`。
- 新增 `config/backends.json`，把 `auto_order` 变成版本化配置。
- Registry 负责注册 document、ocr、layout、table backend factory。
- `loadBackendRegistryConfig()` 校验配置，比如 unknown field、重复 backend、未注册 backend、空数组等。
- `pipeline_service_factory` 从 registry 和配置里解析最终 backend。
- 增加 `tests/unit/backend_registry_test.cpp`。

解决结果：

`auto` 不再是散落在代码里的策略，而是可配置、可校验、可测试的 backend policy。部署时可以改配置选择后端顺序，而不用改 Pipeline 主流程。

可以顺带说的设计模式：Registry + Factory；fallback 顺序有一点 Chain of Responsibility 的味道。

面试说法：

> auto 选择一开始如果写成 if-else，会把部署策略和主流程绑死。我后来加了 BackendRegistry 和版本化 backends.json，让 backend 选择变成配置策略，并且在加载时做完整校验。

### 13.3 SDK 不能只是 CLI 的薄包装

相关提交：

- `c5cd01d feat(sdk): add reusable document engine facade`
- `470b763 refactor(sdk): separate parsing from document export`
- `f5a5de4 feat(sdk): centralize backend configuration`
- `8dc2c78 feat(sdk): return structured engine status`

当时的问题：

如果 SDK 只是复用 CLI 流程，每次 parse 都重新配置、重新创建 backend、直接写文件，对下游嵌入场景不好：

- 业务系统可能希望拿内存里的结构化结果，而不是只拿输出文件。
- 模型 session 反复加载成本高。
- 后端配置散落在 CLI、backend 构造函数、环境变量里，不利于稳定 SDK。
- 初始化失败、parse 失败如果只返回 bool 或打印日志，下游无法判断原因。

怎么改的：

- 新增 `DocumentEngine`，作为可复用 facade。
- `DocumentEngine` 初始化时创建后端服务，`parse()` 返回 `ParseResult`。
- 拆分 parse 和 export：SDK parse 返回 `ParsedDocument` 和 `PipelineArtifacts`，导出 JSON/Markdown/HTML 由调用方决定。
- 新增集中式 `EngineConfig`，把 PaddleOCR、DocLayNet、Paddle Layout、Table Transformer、Tesseract 等配置收敛起来。
- `ParseResult` 里返回结构化 `Status`，包含 code、message、stage、retryable。

实际改动文件包括：

- `cpp/pipeline/document_engine.cpp/.h`
- `cpp/pipeline/document_pipeline.cpp/.h`
- `cpp/pipeline/engine_config.cpp/.h`
- `tests/unit/document_engine_test.cpp`

解决结果：

核心能力从“一次 CLI 命令”变成了可以被服务进程复用的 SDK API。调用方可以复用模型 session、拿内存结果、自己决定导出格式，并且能根据结构化 status 做错误处理。

可以顺带说的设计模式：Facade。但重点是“从 CLI 工具演进成可嵌入 SDK”。

面试说法：

> 我后来发现 CLI 能跑不等于 SDK 好用，所以加了 DocumentEngine。它把 backend 初始化和 parse 生命周期收敛起来，parse 返回结构化结果而不是直接写死导出，这样下游可以复用模型 session，也能自己控制输出格式。

### 13.4 可恢复降级不能被当成失败吞掉

相关提交：

- `d35f40d feat(pipeline): expose partial results and run provenance`
- `4457010 test(contract): validate real partial engine output`
- `7bf70fe feat(contract): trace sources and aggregate warnings`

当时的问题：

模型后端失败不一定意味着整份文档不可用。比如 Layout 模型不可用，可以 fallback 到 text layout；OCR 某些页失败，也可能还有原生文本。早期如果只用成功/失败表达，就会丢掉两个关键信息：

- 最终结果是否可用但降级；
- 降级发生在哪个 stage、哪个 backend、哪些页面。

怎么改的：

- 新增 `Diagnostic`，表示 stage warning。
- 新增 `StageResult<T>`，让 stage 同时返回 value 和 diagnostics。
- 新增 `RunProvenance`，记录 requested/resolved backend 和 fallback。
- `DocumentPipeline` 在每个 stage 收集 diagnostics，最终写入 document warnings。
- `document.status` 支持 `partial`。
- 新增 `warning_aggregator`，把等价 warning 聚合，并保留 page evidence。
- 新增 `file_fingerprint`，把源文件 size 和 SHA256 写入输出。
- `partial_document_contract_producer.cpp` 和 smoke test 用真实 engine 输出验证 partial contract。

实际改动文件包括：

- `cpp/common/diagnostic.h`
- `cpp/pipeline/stage_result.h`
- `cpp/pipeline/run_provenance.h`
- `cpp/pipeline/document_pipeline.cpp`
- `cpp/document/warning_aggregator.cpp`
- `tests/partial_document_contract_smoke.sh`

解决结果：

硬失败和可恢复降级分开了。下游能看到 `complete` / `partial`，也能看到 warning code、stage、page_ids、fallback backend 和源文件 hash。

可以顺带说的设计模式：Result object / structured error。重点是“失败语义细分”。

面试说法：

> 我没有把所有异常都当成解析失败。对于 fallback 后仍可用的结果，我让 Pipeline 返回 partial document，并把 warning、stage、page 和 backend fallback 写进 provenance。这样用户可以判断这个结果能不能接受。

### 13.5 Worker 复用模型 session 后，失败 engine 不能进缓存

相关提交：

- `d3e39de feat(worker): reuse document engine sessions`
- `54fff6d fix(worker): reject failed engines before caching`

当时的问题：

平台 Worker 是常驻进程，如果每个 job 都重新加载模型，冷启动很重。所以引入了 `DocumentEngineCache`。但随后暴露出一个具体 bug：如果请求了不存在的 backend，比如 `ocr=missing`，创建出来的 engine 其实不可用。这个失败 candidate 不应该：

- 被放进缓存；
- 挤掉已有可用 engine；
- 让后续请求命中一个坏 engine。

怎么改的：

`d3e39de` 先加了 engine cache：

- key 是归一化后的 backend tuple：document、ocr、layout、table、registry config；
- 命中后移动到 LRU 头部；
- 超容量淘汰尾部；
- `WorkerDocumentProcessor` 使用 cache 获取 engine。

`54fff6d` 修 bug：

- 创建新 `DocumentEngine` 后先检查 `engine->isReady()`；
- 如果初始化失败，返回 `DocumentEngineLookup{nullptr, status, false}`；
- 不插入 cache，也不触发 eviction；
- 新增测试 `FailedCandidateDoesNotEvictReadyEngines`。

实际改动文件：

- `platform/worker/document_engine_cache.cpp`
- `platform/worker/document_engine_cache_test.cpp`
- `platform/worker/worker_document_processor.cpp`

解决结果：

Worker 既能复用重模型 session，又不会因为一次错误配置污染缓存。失败以结构化 status 返回，已有可用 engine 保留。

可以顺带说的设计模式：LRU cache / resource reuse。重点讲 bug 修复。

面试说法：

> Worker 缓存 engine 后我补过一个 bug：失败初始化的 engine 不能进缓存，也不能挤掉正常 engine。所以 get() 里创建后先检查 isReady，失败就带 status 返回，不写入 LRU。

### 13.6 SDK public header 边界一开始不够干净

相关提交：

- `63b0322 refactor(sdk): enforce public header boundary`

当时的问题：

SDK 对外安装后，下游项目只应该 include 稳定 public header。但之前一些内部类型会从 `cpp/pipeline/`、`cpp/layout/`、`cpp/ocr/` 泄漏出来，导致：

- install consumer 需要知道内部目录；
- public API 和内部实现耦合；
- 后续重构 backend 或 pipeline 时容易破坏下游编译。

怎么改的：

- 把对外头文件收敛到 `sdk/cpp/include/document_intelligence_engine/`。
- 新增 public `options.h`、`provenance.h`、`stage_observer.h`。
- 新增 `cpp/pipeline/document_engine_internal.h` 给内部或测试使用。
- 大幅减少 backend header 暴露的配置细节。
- 增加 install consumer 测试，验证下游只用安装后的 package 也能编译。

实际改动文件：

- `sdk/cpp/include/document_intelligence_engine/document_engine.h`
- `sdk/cpp/include/document_intelligence_engine/options.h`
- `sdk/cpp/include/document_intelligence_engine/provenance.h`
- `sdk/cpp/include/document_intelligence_engine/stage_observer.h`
- `cpp/pipeline/document_engine_internal.h`
- `tests/install_consumer/run.cmake`

解决结果：

SDK public boundary 更清楚，内部 Pipeline/Backend 可以继续演进，下游只依赖稳定安装头文件和 CMake package。

可以顺带说的设计模式：Pimpl / public-private boundary。重点是“安装后的 SDK 真的能被外部项目消费”。

面试说法：

> 我在 SDK 稳定阶段专门做过 public header boundary，把外部可见类型挪到 sdk include 下，内部访问走 document_engine_internal，并用 install consumer 测试防止内部头泄漏。

### 13.7 C ABI 不能继承构建机模型路径

相关提交：

- `1b410cf feat(c-api): add versioned document engine ABI`
- `58ed09a fix(c-api): make configuration portable`
- `d2f885b fix(release): make model paths relocatable`

当时的问题：

C ABI 是给 Java/JNI 或其他语言集成的边界。如果 C ABI 创建 engine 时直接使用 `defaultEngineConfig()`，里面可能包含构建机、开发机或安装前缀里的默认模型路径。这样发布成共享库后，会出现非常隐蔽的问题：

- 在开发机能跑，换机器找不到模型；
- C ABI 用户以为没传模型路径也可以用 Paddle/DocLayNet；
- 结果依赖当前工作目录或构建时路径，不可移植。

怎么改的：

- C ABI 使用 opaque handle：`die_engine_t`、`die_document_t`、`die_error_t`。
- 配置和 parse options 都必须是 `schema_version: 1` 的 JSON。
- `58ed09a` 把 C ABI 的 `EngineConfig config = defaultEngineConfig()` 改为 `EngineConfig config;`，让模型路径在 C 边界默认为空。
- 如果 C ABI 用户选择 `paddle`、`doclaynet`、`paddle-layout`、`table-transformer`，必须显式提供模型路径。
- smoke test 增加 `missing_model_config`，确认缺模型路径时返回 `configure.backend_unavailable`。
- `d2f885b` 进一步处理 release/SDK 中模型路径 relocatable 的问题。

实际改动文件：

- `sdk/c/include/document_intelligence_engine/c_api.h`
- `sdk/c/src/document_intelligence_engine.cpp`
- `docs/c-api-v1.md`
- `tests/c_api_smoke.c`
- `sdk/cpp/include/document_intelligence_engine/engine_config.h`
- `tests/check_release_packaging.sh`

解决结果：

C ABI 不再依赖构建机模型路径。跨语言用户必须显式传配置，错误也通过 `die_error_t` 返回 stage/code/message/retryable。

可以顺带说的设计模式：Opaque handle。重点是“跨机器可移植性 bug”。

面试说法：

> C ABI 初版如果沿用 defaultEngineConfig，会把开发机模型路径带进发布库。我后来改成 C 边界模型路径默认空，选择模型 backend 就必须显式传路径，并加 smoke test 验证缺路径会结构化失败。

### 13.8 PDF 原生文本存在 UTF-16 surrogate pair 问题

相关提交：

- `27e2676 fix(pdf): emit valid UTF-8 for surrogate pairs`

当时的问题：

PDFium 提取文本时会遇到 UTF-16 surrogate pair。如果直接按单个 code unit 转 UTF-8，遇到 emoji、特殊符号或非 BMP 字符时可能输出非法 UTF-8。非法 UTF-8 会影响 JSON 导出、文本评测和下游解析。

怎么改的：

- 新增 `cpp/common/utf8.h`，集中处理 UTF-8 编码。
- 修改 `pdf_text_extractor.cpp`，正确处理 surrogate pair。
- 新增 `tests/unit/utf8_test.cpp`。

实际改动文件：

- `cpp/common/utf8.h`
- `cpp/document_source/pdf/pdfium/pdf_text_extractor.cpp`
- `tests/unit/utf8_test.cpp`

解决结果：

PDF 原生文本输出保证是合法 UTF-8，避免 JSON 导出和文本归一化阶段被坏字符串污染。

面试说法：

> 这个是一个很具体的 PDF 文本 bug：PDFium 出来的 UTF-16 不能简单逐 code unit 转 UTF-8。我加了统一 UTF-8 编码工具和单测，保证 surrogate pair 能正确输出。

### 13.9 文本重复不是靠肉眼看，要做指标

相关提交：

- `7f2990e test(pipeline): enforce 15-page text and order baseline`
- `cb9fdd2 test(pipeline): enforce full-text duplication baseline`

当时的问题：

Pipeline 里既有 PDF 原生文本，又可能补 OCR。一个常见问题是同一段内容被抽两遍：一次来自 text layer，一次来自 OCR。肉眼看 demo 容易漏掉，全文 CER 又会受到顺序和替换错误影响，不适合单独定位重复问题。

怎么改的：

- 先建立 15 页 Pipeline 文本和阅读顺序基线。
- 后续增加 11 页独立 full-text reference。
- `evaluate_pipeline.py` 增加 `text_duplication_rate`。
- 重复率用“归一化字符多重集”计算，不依赖阅读顺序。
- 同时报告 `full_text_reference_coverage` 和 companion `full_text_cer`，避免把部分参考说成全量覆盖。

实际改动文件：

- `tests/benchmark/evaluate_pipeline.py`
- `tests/benchmark/corpus/pipeline_quality/full_text/*.txt`
- `scripts/prepare_pipeline_text_references.py`
- `tests/benchmark/README.md`
- `docs/evaluation.md`

解决结果：

重复抽取有了自动回归门槛，后续改文本合并策略时 CI 能发现“同一内容输出多次”的退化。

面试说法：

> 我们遇到过原生文本和 OCR 合并导致重复的问题，所以不是只看最终文本，而是加了 duplication rate。它用字符多重集统计，和阅读顺序解耦，专门抓重复输出。

### 13.10 表格结构对了，不代表 cell 文字对了

相关提交：

- `9f15c87 test(table): enforce structure-matched text CER`

当时的问题：

表格评测如果只看 Table/Row/Column/Cell bbox 的结构 F1，会漏掉一个问题：结构框都对，但 cell 里的文字可能错，或者 OCR 文本被分配到错误 cell。对于下游 RAG 或数据抽取来说，cell text 错同样不可用。

怎么改的：

- PubTables benchmark 在结构对象之外，加入 cell text reference。
- `evaluate_table.py` 先做结构匹配，再对匹配 cell 计算 text CER。
- `pubtables_table_eval.cpp` 跑真实 OCR、table region crop、structure model 和 cell text assignment。
- CI 同时约束结构 micro-F1 和 table text CER。

实际改动文件：

- `tests/benchmark/evaluate_table.py`
- `tests/pubtables_table_eval.cpp`
- `tests/benchmark/corpus/table_pubtables/ground_truth.json`
- `scripts/prepare_annotated_baseline.py`

解决结果：

表格质量从“框检测正确”推进到“结构匹配后的文字是否可用”。这能定位 OCR-to-cell assignment 和表格结构后处理的问题。

面试说法：

> 表格我没有只测 bbox F1，因为结构对了不代表 cell 内容对。我加了 structure-matched text CER，先匹配 cell，再算文字 CER，这样能发现 cell 文本分配错误。

### 13.11 质量报告不能只是散落的 JSON

相关提交：

- `7dafe23 feat(evaluation): generate traceable quality reports`
- `cf5f02d test(evaluation): drive gates from quality profile`

当时的问题：

评测脚本会生成各种 report，但如果每个 report 都是自己的格式，后续很难比较：

- 不知道用的是哪个 engine 版本；
- 不知道语料版本和 manifest hash；
- 阈值散落在 shell 参数里；
- 新增指标可能悄悄改变旧指标含义。

怎么改的：

- 新增 `generate_quality_report.py`。
- 新增 `pipeline-quality-v1.json` profile。
- 指标从 evaluator report 里通过 JSON Pointer 显式映射。
- Quality Report 记录 subject、corpus、metrics、threshold、artifact 和 hash。
- CI gate 从 profile 读取阈值，而不是散落在命令行。

实际改动文件：

- `tests/benchmark/generate_quality_report.py`
- `tests/benchmark/profiles/pipeline-quality-v1.json`
- `schemas/quality-report.v1.schema.json`
- `tests/benchmark/test_quality_report.py`
- `.github/workflows/pipeline-evaluation.yml`

解决结果：

评测结果开始可追踪、可比较，阈值也集中在版本化 profile 里。新增指标时不会无意改变旧指标语义。

面试说法：

> 后来我把散落的 evaluator report 转成统一 Quality Report，用 profile 显式声明指标来源和阈值。这样 CI gate 和报告语义都可版本化。

### 13.12 平台 artifact 不能半写入就被 API 读到

相关提交：

- `bf682e6 fix(platform): publish artifact manifests atomically`

当时的问题：

Worker 在生成 artifact manifest 时，如果 API 或前端刚好读取到半写入文件，就可能看到不完整 JSON 或缺字段。这个问题在本地不一定稳定复现，但在异步 Worker + API 读取场景里是典型竞态。

怎么改的：

- Worker stage observer 写 artifact manifest 时先写临时文件；
- 写完后再原子 rename 到目标路径；
- 增加 `worker_stage_observer_test.cpp` 覆盖原子发布行为。

实际改动文件：

- `platform/worker/worker_stage_observer.cpp`
- `platform/worker/worker_stage_observer_test.cpp`

解决结果：

API/前端要么看见旧文件，要么看见完整新文件，不会看见半截 JSON。

面试说法：

> 平台异步化后我补过 artifact 原子发布。Worker 不直接写最终 manifest，而是先写临时文件，再 rename，避免 API 读到半写入 JSON。

### 13.13 Worker 镜像不应该内置模型

相关提交：

- `68cf515 fix(platform): keep worker images model-free`
- `f627440 feat(platform): initialize verified models before worker`

当时的问题：

模型文件大，而且许可证、版本和更新节奏跟程序不一样。如果把模型直接塞进 Worker 镜像，会有几个问题：

- 镜像很大；
- 程序小改也要重新分发模型层；
- 离线部署时很难校验模型完整性；
- 模型损坏或版本不匹配时 Worker 可能启动后才失败。

怎么改的：

- Worker 镜像保持 model-free。
- 新增 `model-init` 服务，在 Worker 启动前准备模型。
- `setup_model_pack.sh` 和 `package_model_pack.py` 负责下载/校验模型包。
- 模型 manifest 记录 SHA256。
- Compose 里 Worker 等待 model-init health gate。
- 新增测试检查 worker container/model boundary。

实际改动文件：

- `platform/deploy/docker-compose.yml`
- `platform/deploy/model-init.Dockerfile`
- `platform/deploy/run-model-init.sh`
- `scripts/setup_model_pack.sh`
- `scripts/package_model_pack.py`
- `tests/check_worker_container_model_boundary.sh`
- `tests/check_model_pack_sync.py`

解决结果：

程序镜像和模型包解耦，模型在启动前完成 SHA256 校验，Worker 只在模型准备好后启动。

面试说法：

> 我把 Worker 镜像和模型包拆开，增加 model-init 做下载和 SHA256 校验。这样程序发布不会把模型层一起打包，模型损坏也能在 Worker 启动前发现。

### 13.14 多栏阅读顺序不是简单排序能解决

相关提交：

- `e7bf673 feat(reading-order): improve multi-column parsing quality`

当时的问题：

多栏文档里，如果只按 `y` 再按 `x` 排序，左右栏内容会交错。还有一些 PDF 原生文本本身带损坏控制字符，会让系统误判原生文本可用，导致 OCR fallback 不触发或阅读顺序异常。

怎么改的：

这个提交改动很大，主要包括：

- 新增 `layout_postprocessing.cpp/.h` 和大量单测，做 layout 后处理；
- `reading_order_model.h` 新增 `ReadingOrderTrace`，记录 placements、edge_counts、cycle_breaks；
- `docling_like_reading_order_backend.cpp` 大幅调整阅读顺序算法；
- `text_model.h` 新增 `NativeTextExtractionSignals`；
- `text_quality.cpp` 增加 damaging control codepoint 检测；
- `pdf_text_extractor.cpp` 记录原生文本提取信号；
- `json_document_exporter.cpp` 输出更多 debug/trace 信息；
- 增加 `layout_postprocessing_test.cpp`、`reading_order_backend_test.cpp`、`text_extraction_stage_test.cpp`、`text_normalizer_test.cpp`。

解决结果：

阅读顺序不再只是黑盒排序，debug 输出里能看到 placement 和 cycle break；原生文本质量判断也更细，遇到损坏控制字符时可以走 OCR 或合并策略。

面试说法：

> 多栏顺序我不是简单调排序权重，而是补了 layout 后处理和 reading-order trace。这样既能改善顺序，也能解释为什么某些 block 被放到某个 column/band，排查时更容易定位。

### 13.15 这部分面试怎么讲

推荐把设计问题讲成提交演进：

```text
1. 早期 Pipeline 和 backend 耦合，所以通过 e9ecb56/3bc64b5 拆接口和工厂边界。
2. 后端 auto 策略不能写死，所以 d54e6df 引入 config-driven BackendRegistry。
3. CLI 能跑不等于 SDK 好用，所以 c5cd01d/470b763 做 DocumentEngine 和 parse/export 解耦。
4. 模型失败不一定整份失败，所以 d35f40d/7bf70fe 引入 partial、warning、provenance。
5. Worker 要复用模型 session，但失败 engine 不能污染缓存，所以 d3e39de 后又用 54fff6d 修缓存逻辑。
6. C ABI 不能依赖构建机路径，所以 58ed09a 改成模型路径必须显式配置。
7. 质量问题不能靠 demo，所以 cb9fdd2/9f15c87/7dafe23 把重复率、表格文字 CER 和 Quality Report 做进 CI。
```

最推荐的回答：

> 我不会说这些设计是一开始就完美规划好的，更多是提交演进出来的。比如先发现 Pipeline 和 backend 耦合，就拆接口；发现 auto 策略写死不好维护，就做 BackendRegistry；发现 SDK 不能只复用 CLI，就抽 DocumentEngine；发现 fallback 后结果仍可用，就引入 partial/provenance；发现 Worker 重复加载模型，就做 engine cache，后来又修了失败 engine 不能进缓存的问题。这些改动都能在提交里对应到具体文件和测试。

## 14. 最推荐的面试表达

最后建议你把这个项目讲成：

> 这是一个文档智能工程化项目，不是单模型调用项目。我做的重点是把 PDF 解析拆成稳定 Pipeline，用 Backend 抽象接入不同模型，用 Document Contract 稳定下游输出，用 warning/provenance 解释降级，用多层评测和 CI 防止回退，再通过 SDK、C ABI、CLI、Docker 和可选平台完成交付。

这个表达能突出你的工程能力：架构拆分、抽象设计、质量体系、部署意识和对 AI 模型不确定性的处理。
