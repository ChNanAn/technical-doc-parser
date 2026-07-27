# Warning Code 注册表

Document v1 使用 Warning 表示“结果仍可使用，但生成过程发生了明确降级”。机器读取稳定的 `code`，供人阅读
的 `message` 可以在不改变 Code 的情况下持续优化。

引擎代码层注册表位于
[`cpp/common/warning_codes.h`](../cpp/common/warning_codes.h)。Document v1 不把这些值封闭成 JSON Schema
枚举：v1 的兼容版本可以增加新 Code，嵌入方也可以增加带命名空间的扩展 Code。

## 引擎 Code

| Code | Stage | 含义 | 稳定 Details |
| --- | --- | --- | --- |
| `OCR_ENHANCEMENT_FAILED` | `text` | 可用原生文本需要 OCR 增强，但 OCR 失败，因此保留原生文本。 | `fallback=native_text`；`reason` 表示原生文本质量判断原因。 |
| `LAYOUT_BACKEND_FALLBACK` | `layout` | Layout Backend 运行失败，由配置链中的下一个 Backend 生成本页结果。 | `failed_backend`、`fallback_backend`、`reason=inference_failed`。 |
| `TABLE_BACKEND_FALLBACK` | `table` | Table Backend 运行失败，由配置链中的下一个 Backend 生成本页结果。 | `failed_backend`、`fallback_backend`、`reason=inference_failed`。 |

## 兼容规则

- Document v1 生命周期内，已有 Code 的 Stage 和含义保持不变。
- 新 Code 只能以兼容方式增加，消费者必须容忍未知 Code。
- `message` 只供人阅读，不能作为程序判断依据。
- 已注册 Detail Key 的含义保持稳定，但可以增加新的 Detail Key。
- 应用自定义 Code 应使用不易冲突的前缀，例如 `ACME_OCR_REVIEW_REQUIRED`。

引擎一旦产生 Warning，文档状态就是 `partial`。致命 Stage 失败通过 `Status` 返回，不生成可用的
Document v1 结果。
