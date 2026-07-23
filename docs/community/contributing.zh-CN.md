# 贡献指南

感谢参与 Document Intelligence Engine。项目目前是一个早期、可运行、可评测的 C++ 文档解析引擎，不是
已经完成的企业产品。我们更欢迎范围明确、带测试和证据的小改进，而不是大规模重写或没有评测的新 Backend。

## 当前最需要帮助的方向

1. Job 恢复、Attempt、幂等、取消和模型 Session 复用。
2. 真实技术文档的端到端评测。
3. OCR、Reading Order、Document Assembly 和来源可追溯的 RAG 输出。
4. 文档、公开 Fixture、可视化诊断和可复现 Bug。

项目暂时不优先扩展 Office 输入、大量同类模型、多租户 SaaS、Kubernetes 或 GPU 调度。

领取任务前请阅读[路线图](../roadmap.md)。大型功能应先通过 GitHub Discussions 或 Issue 确认问题、范围和
验收标准。

## 提交 Issue 前

- 搜索是否已经有相同问题。
- 提供 Backend 组合、构建配置、完整命令和相关日志。
- 尽量使用最小、允许再分发的文档复现。
- 删除机密信息，不要擅自上传客户或公司的文档。
- 准确率改动需要说明要改善的指标，并提供 Fixture 或公开数据来源。

新增模型 Backend 必须包含不可变下载地址、SHA256、License/来源说明、标签映射、预处理/后处理说明和
可持续运行的回归测试。

## 本地验证

核心引擎：

```bash
cmake --preset core-release
cmake --build --preset core-release --parallel
ctest --preset core-release
```

可选平台 Worker：

```bash
cmake --preset platform-release
cmake --build --preset platform-release --target document_intelligence_worker --parallel
```

API 和 Web：

```bash
python -m pip install -e './platform/api[test]'
pytest platform/api/tests
npm ci --prefix platform/web
npm audit --prefix platform/web
npm run build --prefix platform/web
```

依赖安装见 [docs/dependencies.md](../dependencies.md)，提交格式见
[docs/commit-convention.md](../commit-convention.md)。

## Pull Request 要求

- 一个 PR 只解决一个问题。
- 先解释已经确认的原因，再说明修改方案。
- 解决方案应适用于一类输入，不能只特殊处理一个 PDF。
- 根据风险增加测试、Fixture 或 Benchmark 证据。
- 保持 Backend 中立，并保留页码、bbox、置信度和来源信息。
- 不提交模型、私有文档、构建目录或运行产物。
- 行为、配置或已知限制发生变化时同步更新文档。

维护者精力有限，Review 可能要求缩小范围，也可能暂缓当前里程碑之外的功能。这是项目优先级选择，不是
对想法价值的否定。

## 数据与许可证

只提交允许再分发的数据。必须记录来源 URL、不可变版本、License、预期文件和 SHA256；派生标注也必须
保留原数据集要求的署名。提交代码表示同意贡献内容按照仓库的 MIT License 分发。
