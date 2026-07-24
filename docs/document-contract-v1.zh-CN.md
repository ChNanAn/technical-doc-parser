# Document Contract v1 设计说明

[English](document-contract-v1.md)

Document v1 是引擎面向下游消费者的结果契约。机器可读 Schema 位于
[`schemas/document.v1.schema.json`](../schemas/document.v1.schema.json)，示例位于
[`schemas/examples/document.v1.example.json`](../schemas/examples/document.v1.example.json)。

当前状态是 release candidate。公共 C++ 模型和 `document.json` exporter 已共享 Page、Block、Relation、
Warning 和 Source Reference 语义；仅用于调试的 Pipeline Artifacts 位于命名空间 `extensions` 字段，不进入
稳定核心。

## 核心原则

先建立一个宽松但可解释的输出外壳，再用三层评测持续约束实际质量，而不是试图用一个严苛的 Schema
一次性规定所有未来能力。

Document v1 只冻结下游必须依赖的含义：

- 契约版本、来源身份、生产者身份和结果状态。
- 页面身份以及唯一明确的坐标约定。
- 顶层 `blocks` 数组表示最终阅读顺序。
- Block 和 Relation 的引用范围。
- 可用但发生降级时的机器可读 Warning。

它不冻结模型标签全集、Backend 名称、所有可能的 Block 属性，也不强制要求一张完整的 provenance graph。
这些部分需要随着真实技术文档评测继续演进。

通过 Schema 只表示“结果能被一致解释”，不表示 OCR、Layout、Table、Reading Order 或 RAG 引用已经准确。
质量结论由[三层评测](evaluation.zh-CN.md)负责。

## 输入范围

这份契约不限制 PDF。`source.media_type` 使用 MIME Type，因此同一个输出外壳可以描述 PDF、页面图片、
Office 文档以及未来支持的其他来源。

当前实现从 PDF 开始，是因为 PDFium 仍是唯一完整的 Document Source Adapter。未来增加输入格式属于引擎
能力扩展；只要仍生成相同的页面化、可定位结果，就不需要修改 Document v1。

## 必需外壳

| 字段 | 稳定含义 |
| --- | --- |
| `$schema` | Document v1 的规范 URI。 |
| `schema_version` | 主版本，v1 固定为 `1`。 |
| `document_id` | Producer 范围内的文档标识，不能被当作文件路径。 |
| `status` | `complete` 或仍可使用但发生降级的 `partial`。 |
| `source` | 来源 MIME Type 和可选的安全身份字段。 |
| `producer` | 引擎名称与版本，可附带 Git revision 和 Run ID。 |
| `coordinate_space` | 所有页面共享的坐标含义。 |
| `pages` | 用于来源定位和可视化的页面。 |
| `blocks` | 按最终阅读顺序排列的文档块。 |
| `warnings` | 稳定降级码和供人阅读的上下文。 |

`relations`、`metadata` 和 `extensions` 是可选字段。为了前向兼容，Schema 允许未知字段；实验性或
Provider 特有数据仍应优先放入带命名空间的扩展键，例如 `extensions["org.example.layout"]`，避免命名冲突。

## 坐标约定

所有公共 bbox 只使用一套约定：

```text
unit: pixel
origin: top_left
bbox_format: xyxy
bbox: [x0, y0, x1, y1]
```

坐标可以是整数或小数。bbox 必须有正面积，并位于引用页面内部：

```text
0 <= x0 < x1 <= page.width
0 <= y0 < y1 <= page.height
```

页码从 1 开始，引用使用 Page ID，不使用数组下标。坐标对应最终输出且方向已经归一化的页面图像，不能
暗中使用另一套 PDF 坐标。

这是 v1 最严格的部分，因为坐标含义变化会悄悄破坏 Viewer、来源引用和训练数据生成。

## Blocks 与阅读顺序

每个 Block 只强制要求 `id` 和 `type`。`text`、`page_id`、`bbox`、`score`、`source_refs`、`table`、
`metadata` 和 `extensions` 都是可选的，因为不同 Backend 未必能可靠提供这些信息。

顶层 `blocks` 数组顺序就是最终全局阅读顺序。除非消费者明确要替换引擎结果，否则不应再次按 bbox 排序。

`type` 使用开放字符串，不使用封闭枚举。Producer 应优先使用 `title`、`heading`、`paragraph`、`list`、
`table`、`figure`、`caption`、`formula`、`header`、`footer`、`unknown` 等常见值，但 Schema 不拒绝新类型；
消费者必须保留或容忍未知值。

字段可选不代表缺失就是高质量。例如，没有页面定位的 Block 在结构上合法，但会降低来源定位与 RAG 引用
完整率。质量问题交给指标暴露，而不是让 Producer 为了过 Schema 伪造信息。

## 表格

Table Block 可以包含有序 Rows 和 Cells。Cell 只强制要求 `text`；行列下标、Span、表头标记、bbox、
Score 和来源引用都可选。能力较弱的 Backend 可以诚实地产出 Partial Result，而不是猜测结构。

字段存在时遵循：

- Row 和 Column 下标从 0 开始。
- Span 省略时含义为 `1`。
- `is_header` 必须显式表达，消费者不能默认第一行是表头。
- Cell bbox 使用所属 Table Block 的页面坐标。
- 跨页表格片段可以共享 `continuation_group_id`。

表格结构质量和表格文字准确率应分别评测。Schema 校验通过不代表表格解析准确。

## 可解释性的边界

Document v1 保留下游日常使用所需的解释信息：

- `producer` 说明哪个引擎版本生成了结果。
- Page ID 和 bbox 能把结果定位回可见来源。
- `source_refs` 可以保留更细粒度的原始区域或文本证据。
- `warnings` 使用稳定 Code 解释 Partial Result。
- `extensions` 可以承载额外 Provider 信息而不污染公共核心。

完整执行过程属于独立的 Pipeline Trace 契约，包括每个 Stage 的尝试、请求与实际 Backend、fallback 原因、
耗时、模型身份、错误和 Debug Artifact。将其与 Document v1 分开，可以避免 Worker 内部状态永久固化为 SDK 字段。

## 兼容策略

v1 内兼容的变化包括增加可选字段、Warning Code、Block Type、Relation Type 或扩展数据。消费者只读取自己
需要的字段，并忽略不认识的可选字段。

以下变化必须升级到 v2：

- 删除或重命名必需外壳字段。
- 修改坐标单位、原点、bbox 顺序或 Page 引用范围。
- 不再让 `blocks` 表示阅读顺序。
- 重新解释 `complete` 或 `partial`。
- 对既有字段进行不兼容的语义修改。

开放 Vocabulary 不等于没有治理。某个扩展字段只有经过 Fixture、消费者和评测证明语义稳定后，才应该进入
正式文档词汇。

## 契约测试

JSON Schema 负责公共格式，Semantic Tests 额外验证：

- Page ID 和 Block ID 唯一。
- 已提供的 Page、Block、Warning、Relation、Source Reference 都能解析。
- bbox 有正面积且不超出页面。
- Table Cell bbox 位于所属 Table Block 页面中。
- `partial` 结果至少包含一个 Warning。
- Source Filename 和 Artifact URI 不泄露本机绝对路径。

Contract Fixtures 至少覆盖原生文本、扫描 OCR、复杂表格和多栏阅读顺序。Snapshot 用于保护公共输出的有意
变更；Quality Corpus 用于判断内容是否真的进步。
四类已评审 Snapshot 位于
[`tests/contract/snapshots/document-v1`](../tests/contract/snapshots/document-v1)。

## 落地顺序

1. 建立四类 Contract Fixtures，并验证有代表性的非 PDF MIME Type。
2. 将公共 C++ Document Model 映射到该外壳，不混入 Exporter 私有字段。
3. 让 JSON、Markdown、HTML 和未来 RAG Exporter 消费同一个组装结果。
4. 单独建立 Pipeline Trace v1，记录 Backend 选择、fallback、耗时和失败。
5. 使用版本化 Quality Report 作为发布门槛，而不是不断增加 Schema 必填项。
