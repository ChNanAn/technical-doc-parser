# 三层质量评测

[English](evaluation.md)

项目需要把“格式能否使用”和“结果质量是否足够好”分开：

- [Document Contract v1](document-contract-v1.zh-CN.md)定义结果能否被下游一致解释。
- Backend、Pipeline、Product 三层评测判断引擎是否真的准确、可用、高效。
- [Quality Report v1](../schemas/quality-report.v1.schema.json)记录可比较的结果，完整示例见
  [`quality-report.v1.example.json`](../schemas/examples/quality-report.v1.example.json)。

JSON 通过 Schema 不等于解析正确。反过来，一个诚实的 Partial Result 应保持结构合法并在质量指标中扣分，
不应该为了满足 Schema 而伪造 bbox、表头、来源或置信度。

## 三层分别衡量什么

| 层级 | 回答的问题 | 核心指标 |
| --- | --- | --- |
| Backend | 单个模型或 Provider 的本职任务做得怎样？ | OCR CER/WER、Layout F1、Table Structure F1 |
| Pipeline | 多个 Stage 组合后是否保住并正确组织了内容？ | 文本完整率与重复率、Block Type F1、Reading Order、表格文本 |
| Product | 下游拿到的交付结果是否真的可靠？ | JSON 兼容性、成功率、RAG 引用完整率、耗时、内存 |

Backend 指标用于定位组件能力；Pipeline 指标负责发现单模型评测看不到的集成问题，例如原生文本丢失、OCR
重复、Block 组装错误或阅读顺序错乱；Product 指标衡量用户真正拿到的 API 与运行结果。某个 Backend
分数很高，不能替代缺失的 Pipeline 或 Product 评测。

## 指标口径

### Backend 层

- `ocr_cer` / `ocr_wer`：在版本化文本归一化规则后，用全语料 Levenshtein 编辑距离除以参考字符数或词数。
  缺失预测按空文本计分，不能从分母中删除。
- `layout_micro_f1`：在记录明确的 IoU 阈值下，按类别执行一对一 bbox 匹配。报告同时保留 Precision、
  Recall、Macro F1、各类别计数和 Backend Label Mapping。
- `table_structure_micro_f1`：对 Table、Row、Column、Header、Spanning Cell 等结构对象做分类一对一匹配，
  不把文字正确率混入结构分数。

### Pipeline 层

- `text_completeness`：最终归一化文档文本中能与参考文本对齐的字符数 / 参考字符总数，用来发现从
  Extraction/OCR 到 Assembly 之间丢失的内容。
- `text_duplication_rate`：最终文本中无法与参考内容对齐的字符数 / 最终字符数。该指标必须与 CER 一起看，
  避免把错误替换文字都误判为纯重复。
- `block_type_micro_f1`：最终 Blocks 与标注 Blocks 在指定 IoU 阈值和 Taxonomy Mapping 下做分类一对一匹配。
- `reading_order_score`：匹配内容块中顺序正确的 Block Pair 数 / 全部可比较 Block Pair 数，是 `[0, 1]`
  的归一化 Kendall-style 分数。少于两个匹配 Block 的页面单独统计，不能抬高均值。
- `table_text_cer`：先匹配表格结构与 Cell，再计算 Cell Text 的全语料 CER。它补充 Table Structure F1，
  不能被结构分数替代。
- 如果最终输出会过滤 Header/Footer，还应分别统计过滤的 Precision 和 Recall。

文本归一化规则、IoU 阈值、标签映射、空样本处理方式都属于指标版本。改变这些口径时必须升级 Metric 或
Corpus Version，不能悄悄修改公式后继续画同一条趋势线。

### Product 层

- `document_success_rate`：产生可用 `complete` 或有明确 Warning 的 `partial` 结果的文档数 / 全部尝试文档数。
  Timeout、Crash、任务永久丢失、无效输出都算失败。
- `json_contract_valid_rate`：可用结果中通过所选公共契约校验的比例。Contract Fixture Snapshot 额外检测字段
  被意外删除或语义变化；Snapshot 必须经过 Review，不能失败后直接覆盖。
- `rag_citation_completeness`：来源引用能解析到已有 Page 和合法页面区域的 Chunk 数 / 全部输出 Chunk 数。
  未来 RAG Chunk Contract 再增加引用文字一致率和 Retrieval 指标。
- `latency_ms_per_page_p50/p95`：按页数归一化的 Wall-clock Latency，同时记录硬件、并发、冷/热启动和
  Backend 配置。
- `peak_rss_mib_p50/p95`：同一受控运行下的进程峰值常驻内存；GPU 显存单独报告。

## Quality Report v1

Quality Report v1 固定 `backend`、`pipeline`、`product` 三个层级，但指标名使用开放字符串。每个 Metric
记录数值、单位、优化方向，以及可选 Threshold、分子分母、样本量和 Detail。报告还必须说明：

- 引擎版本、Git revision 和配置身份。
- Corpus 名称、版本、Split、样本量和可选 Manifest SHA256。
- 总状态：`passed`、`failed` 或 `incomplete`。
- 可选 Baseline Report 和逐文档 Artifact。

开放指标名允许团队继续增加评测而不修改 Schema。真正保证可比性的是清楚的指标语义和版本化 Corpus，
不是一份封闭 Enum。

## 数据集分层

1. **Contract Fixtures**：原生文本、扫描 OCR、复杂表格、多栏阅读顺序。规模足够小，每次 CI 都执行，用于
   Schema、引用语义和经过 Review 的 Snapshot Tests。
2. **固定 Regression Corpus**：代表性的技术手册、规格书、图纸和表格密集报告。定时 CI 执行，输出完整
   三层质量报告。
3. **外部 Validation Corpus**：更大或私有的行业数据。发布前验证泛化能力；无法公开原文时可只发布聚合报告。

每个 Corpus 都应有 Manifest，记录稳定 Document ID、内容 Hash、来源与 License、Annotation Version、
Train/Test 隔离。仓库中五页规模的模型数据只能作为回归报警器，不能证明生产准确率。

## 仓库已有 Backend 基线

### 已提交的 PaddleOCR 回归

必需的 ONNX Build 会在 Tesseract 测试语料的 5 个可再分发页面上运行真实 PaddleOCR 检测与识别。C++
Producer 输出页级文本后，Backend-independent Evaluator 统一执行 NFKC、空白折叠、大小写归一化，并计算
Corpus CER/WER：

```bash
ctest --test-dir build-ort -R paddle_ocr_benchmark --output-on-failure
```

固定 `ppocrv5_mobile` baseline 的 Corpus CER 为 `0.6827`，字符数量比为 `0.9714`，WER 为 `0.8487`；CI
要求 `CER <= 0.70` 并上传 `paddle_ocr_report.json`。高 CER 真实暴露了两个已知限制：多栏杂志页的文本按
横向行交错输出，且 baseline 尚不支持 180 度方向处理。两个简单正向页面的 CER 分别为 `0.0170` 和
`0.0000`。这 5 页是确定性的回归门槛，不代表广泛场景准确率。

### FUNSD OCR

下载数据并构建评测目标：

```bash
bash scripts/download_funsd.sh
cmake -S . -B build-ort -DCMAKE_BUILD_TYPE=Release
cmake --build build-ort --config Release --target funsd_ocr_eval --parallel
```

先跑小规模 Smoke，再运行完整 Testing Split：

```bash
./build-ort/tests/funsd_ocr_eval \
  --funsd-root data/raw/funsd/dataset \
  --split testing_data \
  --limit 5 \
  --report output/funsd_ocr_eval_5.json
```

现有报告包含 `ok_rate`、`corpus_cer`、`detection_recall`、`gt_crop_recognition_cer` 和逐页计数。FUNSD
按 Word 标注而 PaddleOCR 检测 Text Line，因此当前 Coverage Recall 有效，但不能把 Line Count 与 Word Count
直接称为 Detection Precision。

### DocLayNet Layout

仓库包含五页可再分发 Fixture。ONNX 构建使用 IoU `0.5` 的分类最大一对一匹配：

```bash
ctest --test-dir build -R '^(doclaynet_layout_benchmark|paddle_layout_benchmark)$' --output-on-failure
```

当前 RF-DETR Micro F1 为 `0.768657`，Paddle PP-DocLayoutV3 为 `0.500000`；CI 回归下限分别为 `0.70`
和 `0.45`。不同 Label Taxonomy 会影响比较，这些数字不是全领域准确率结论。

### PubTables-1M Table Structure

```bash
ctest --test-dir build -R pubtables_table_benchmark --output-on-failure
```

五张 Fixture 上当前 Table Structure Micro F1 为 `1.0`，CI 下限为 `0.95`。它能保护 Region Crop、Tensor
Decode、Label Mapping 和结构后处理，但尚未衡量表格文字、TEDS 或真实跨页表格质量。

## 15 页 Pipeline 基线

定时和手动触发的 Workflow 使用同一个可复用 `DocumentEngine` 解析 8 份公开 PDF 中的 15 个已复核页面，
再用最终有序 `DocumentBlock` 对照 2,280 个独立复核字符和 77 个 Reading-order Anchors。

| 指标 | 当前基线 | 回归下限 |
| --- | ---: | ---: |
| 采样文本完整率 | 0.8934 | 0.88 |
| Reading-order Anchor Recall | 0.8571 | 0.84 |
| Pairwise Reading-order Score | 0.9385 | 0.92 |

当前匹配 2,037 个复核字符和 66 个 Anchor，130 个可比较 Pair 中有 122 个顺序正确。其中一张 IRS W-4
Worksheet 页面目前只生成一个空 Header Block，文本完整率和 Anchor Recall 都是 0；该失败被有意保留在
Corpus 和报告中，作为可见的改进目标。这些下限只保护固定语料和固定模型策略不发生回退，不是生产验收线。
复现命令、指标范围和语料来源见 [Benchmark 指南](../tests/benchmark/README.md)。

## 外部验证

项目第一次完整运行独立的 1,403 页 olmOCR-Bench，在 8,413 条测试上得到 `44.2% +/- 0.9%`。Header/Footer
过滤（`94.5%`）和基础输出健康度（`92.4%`）相对可靠；Table（`50.7%`）和 Multi-column Order（`52.3%`）
仍处于早期，Old Scans 只有 `16.3%`，结构化公式输出为 `0%`。

项目有意公开这个并不漂亮的分数：它是可复现的外部起点，不是生产准确率声明。后续 OCR、Reading Order、
Table 和公式导出的改进可以沿同一条独立曲线比较。版本、计数、命令和局限见
[完整 olmOCR-Bench 报告](benchmarks/olmocr-bench.md)。

## 接下来实施顺序

1. 建立四类 Contract Fixtures 和版本化 Technical Document Manifest。
2. 在现有 Completeness/Order Evaluator 上增加 Text Duplication 和 Block Type F1。
3. 增加结构匹配后的 Table Text CER。
4. 校验导出的 Document v1，并建立经过 Review 的 Snapshot Tests。
5. 在统一端到端 Runner 中测量成功率、RAG 引用完整率、p50/p95 耗时和 Peak RSS。
6. 输出 `quality-report.v1`，并在 CI 中与固定 Baseline 比较。

只有当 Metric 和 Corpus 都稳定后才设置 Regression Floor。下限用于防止已知回退，不应自动被当作生产验收线。
