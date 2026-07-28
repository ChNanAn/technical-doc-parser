# 第一批贡献任务草稿

首次发布建议从下面选择 6 个建立 GitHub Issue。每个任务都要求先复现和测量，再修改实现。

## 1. Backend Adapter 最小开发教程

标签：`good first issue`、`area:docs`、`difficulty:easy`

目标：用 Noop/Text Backend 说明 capability、注册、配置、类型归一化和测试流程。

验收：

- 新增独立教程和最小代码片段。
- 说明 public SDK、private pipeline 和 provider object 的边界。
- 包含 CMake 接入和可运行测试命令。

不包含：接入新的模型。

## 2. 补齐 90/270 度 OCR 方向 Fixture

标签：`good first issue`、`area:ocr`、`area:evaluation`、`difficulty:easy`

目标：在已有 180 度样本基础上建立 0/90/180/270 度可再分发回归集。

验收：

- 记录来源、License、SHA256 和生成方式。
- evaluator 按方向报告 CER，缺失预测按空文本计分。
- 测试覆盖四个方向，且不改变现有总分口径。

不包含：实现方向检测。

## 3. 页面方向检测与坐标回映射

标签：`help wanted`、`area:ocr`、`area:core`、`difficulty:medium`

目标：在 OCR/Layout 前检测页面方向，并把结果坐标稳定映射回 Document v1 页面坐标。

验收：

- 先用 Issue 记录候选算法、置信度和 fallback 策略。
- 四方向 Fixture 的 CER 明显改善，正常方向样本不回退。
- 最终 bbox 仍符合 `top_left`、`xyxy` 和页面边界约束。
- 低置信度时产生注册表中的结构化 warning。

不包含：任意角度 deskew。

## 4. 建立多栏 Reading Order 失败语料

标签：`help wanted`、`area:reading-order`、`area:evaluation`、`difficulty:medium`

目标：从允许再分发的公开文档中选取多栏、跨栏标题、sidebar 和浮动图表页面，建立可解释真值。

验收：

- 至少 10 页，记录来源、License 和 SHA256。
- 标注 anchor、顺序和页面结构类型。
- 现有 evaluator 输出 anchor recall、pairwise order score 和首个错误 pair。
- 当前失败必须保留，不能只选择已经通过的页面。

不包含：修改 Reading Order 算法。

## 5. 改进多栏 Reading Order 启发式

标签：`help wanted`、`area:reading-order`、`difficulty:advanced`

目标：改善跨栏标题、双栏正文和后续 full-width block 的排序。

验收：

- 先通过日志定位至少两类通用错误原因。
- 新失败语料和 olmOCR-Bench multi-column 子集得分上升。
- 单栏、header/footer、caption relation 现有测试不回退。
- 规则基于可解释几何关系，不包含文档 ID 或固定页面特判。

不包含：引入新的大型模型。

## 6. 旧扫描件预处理 A/B 基线

标签：`help wanted`、`area:ocr`、`area:evaluation`、`difficulty:medium`

目标：测量 binarization、denoise、contrast 和 deskew 对旧扫描件 OCR 的真实收益。

验收：

- 使用公开、可再分发语料并记录 License。
- 每个策略报告 CER、耗时和 Peak RSS，不只展示最佳图片。
- 通过 profile/config 选择策略，不按文件名特殊处理。
- 技术文档正常扫描页不能出现显著回退。

不包含：训练新 OCR 模型。

## 7. 基于 C ABI v1 的最小 Java/JNI Binding

标签：`help wanted`、`area:java`、`area:sdk`、`difficulty:advanced`

目标：提供第一个 Java 绑定，验证 C ABI 的生命周期和错误模型能被真实语言消费者使用。

验收：

- Java handle 使用 `long`/`AutoCloseable` 管理，重复 close 安全。
- JNI 只依赖安装后的 `c_api.h` 和共享库，不包含 C++ STL 类型。
- 覆盖 create、parse、Document JSON、结构化 error 和 busy 状态。
- Linux x86-64 CI 编译并运行一个公开 PDF smoke test。

不包含：Maven Central 发布、Windows/macOS native 包。

## 8. C ABI 取消机制设计

标签：`area:sdk`、`status:needs-design`、`difficulty:advanced`

目标：为长推理增加协作式取消，同时保持 ABI v1 append-only 和现有 timeout 语义。

验收：

- 设计文档比较 cancel token、engine cancel 和 per-parse handle。
- 明确线程安全、竞态、销毁顺序和稳定错误码。
- 说明 Backend 无法立即中断时的最坏延迟。
- 设计通过后再拆实现 Issue。

不包含：强杀线程或进程。

## 9. 无盘输入与临时页面存储边界

标签：`area:sdk`、`status:needs-design`、`difficulty:advanced`

目标：允许调用者提供内存 PDF bytes，并让页面渲染可选择内存或受控临时存储。

验收：

- 设计不把输入 buffer 生命周期泄漏到异步阶段。
- 定义内存上限、临时目录策略和错误返回。
- 文件路径入口保持兼容。
- C++ API 先稳定，再决定是否追加 C ABI。

不包含：Office/HTML 输入 Backend。

## 10. 发布 SBOM 与 GitHub Attestation

标签：`area:release`、`difficulty:medium`

目标：在现有 SHA256 和 License manifest 基础上，为源码和 Linux CLI 增加机器可验证供应链信息。

验收：

- 生成 SPDX 或 CycloneDX SBOM，包含 PDFium、ONNX Runtime 和程序版本。
- 使用 GitHub artifact attestation 关联 tag、commit 和产物 digest。
- release smoke 验证 SBOM 存在且关键组件版本正确。
- 不在仓库提交生成的发布产物。

不包含：跨平台代码签名。
