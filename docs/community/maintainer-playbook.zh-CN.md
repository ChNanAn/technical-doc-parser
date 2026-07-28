# 单人维护者执行手册

目标不是一个人完成所有模块，而是让外部贡献者能在一小时内理解边界、找到任务，并提交一个可验证的小 PR。

## 当前主线

1. 发布 `v0.1.0`，保证源码、CLI、模型、容器和许可证信息可验证。
2. 质量攻坚：多栏 Reading Order、页面方向、旧扫描件预处理。
3. 分发出口：先做 Java/JNI，再评估其他语言绑定。
4. SDK 能力：无盘输入、取消、真实 source refs 和 RAG Chunk Contract。

数学公式结构识别暂不作为维护者主线。它需要专用模型和输出语义，当前应在 Roadmap 中保持明确的已知限制，
不能用普通 OCR 参数调整冒充支持。

## 第一次发布

1. 推送本地提交，等待 `main` 所有 required checks 通过。
2. 在干净目录运行 CLI bundle 和独立模型包。
3. 创建首批 6 个 Issue，不一次发布几十个空任务。
4. 创建并推送 annotated `v0.1.0` tag。
5. 检查 GitHub Release、GHCR、`SHA256SUMS`、License notices 和模型 manifest。
6. 选择一个中文渠道和一个海外渠道发布。
7. 发布后一周优先回复 Issue 和 Review 小 PR，暂缓大型重构。

## 推荐标签

- 入口：`good first issue`、`help wanted`
- 领域：`area:core`、`area:ocr`、`area:reading-order`、`area:evaluation`
- SDK：`area:sdk`、`area:java`、`area:release`
- 其他：`area:platform`、`area:docs`
- 难度：`difficulty:easy`、`difficulty:medium`、`difficulty:advanced`
- 状态：`status:needs-design`、`status:ready`、`status:blocked`

## Issue 进入开发前

一个 `status:ready` Issue 至少包含：

- 已确认的问题和影响范围，不只是解决方案名称。
- 相关模块或 public contract 边界。
- 可合法再分发的 Fixture、日志或复现命令。
- 可以自动判断的验收标准。
- 建议测试或 Benchmark 命令。
- 明确不在本 Issue 中处理的内容。

准确率任务必须有现状指标和目标指标。新增数据必须记录来源、License、不可变 revision 和 SHA256。

## PR Review 顺序

1. 是否解决 Issue 中已经确认的原因。
2. 是否适用于一类文档，而不是只特殊处理一个 PDF。
3. 是否破坏 Document Contract、C ABI、坐标、来源或错误模型。
4. 是否提供与风险相匹配的测试和质量证据。
5. 是否引入新的模型、下载、License、内存或部署风险。
6. 最后检查命名、格式和局部实现。

涉及公开 ABI、Schema required 字段、坐标语义或模型许可证的 PR，不应作为“顺手修复”合并。

## 每周维护预算

- 20 分钟：分类新 Issue，要求补充复现和验收标准。
- 40 分钟：帮助新贡献者缩小任务范围。
- 60 分钟：Review 文档、测试和小型代码 PR。
- 30 分钟：更新一个质量数字或公开失败案例。
- 30 分钟：维护 Release、依赖和安全更新。

如果本周没有 Review 时间，暂停宣传；不要持续引流后让外部 PR 长期无人回应。

## 固定回复

需求过宽：

> 当前维护资源集中在多栏/旋转/旧扫描质量、Java/JNI 和可追溯 SDK 能力。请补充最小复现、Backend
> 配置、日志和希望改善的指标；确认属于当前主线后再拆成可验收的小任务。

新增 Backend：

> 请先说明现有 Backend 在哪个公开 Fixture 或指标上失败，并提供模型不可变来源、SHA256、License、
> 标签映射和推理前后处理说明。没有可衡量增量的 Backend 暂不接入。

私有文档：

> 请不要上传客户或公司文件。可以提交脱敏后的最小复现，或使用允许再分发的公开文档重建问题。

## 社区健康指标

不要只看 Star。每月记录：

- 首次贡献者数量和一个月后仍参与的人数。
- Issue 首次响应时间、PR 首次 Review 时间。
- 从领取 Issue 到首个 PR 的转化率。
- 合并的小型外部 PR 数量。
- 新增的公开失败 Fixture 和可比较质量报告数量。
- 文档用户成功构建 CLI、C++ SDK 或 Java binding 的真实反馈。

访问量高但没有 PR，通常意味着环境太重、任务太大或验收标准不清楚，不一定是宣传不足。
