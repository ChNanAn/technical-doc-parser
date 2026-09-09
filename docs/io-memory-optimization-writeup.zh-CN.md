# I/O 优化和内存优化复盘

这份文档用于面试和项目讲解。重点不是泛泛说“我做了性能优化”，而是说明：

- I/O 优化和内存优化分别是什么；
- 做这类优化时会先考虑哪些方向；
- 这个仓库里已经用到了哪些方法；
- 哪些地方还只是后续优化方向，不能说成已经完成。

## 1. I/O 优化是什么

I/O 指程序和外部系统交换数据的过程。这里的外部系统包括：

- 磁盘文件：读 PDF、写 PNG、写 JSON/Markdown/HTML、读写模型文件；
- 网络：下载模型、API 调用、Redis/PostgreSQL 通信；
- 进程边界：CLI 输出、C ABI 返回 JSON、Worker 和 API 共享 artifact；
- 容器/部署卷：模型目录、runtime artifact 目录、数据库 volume。

I/O 优化的目标通常有几个：

- 减少不必要的读写次数；
- 避免一次性把大文件读进内存；
- 降低磁盘、网络、数据库等待时间；
- 避免半写入文件被其他进程读到；
- 让失败、重试、缓存和校验更可控；
- 控制 artifact、日志、事件流等长期增长。

一句话说：

> I/O 优化就是减少和外部系统交换数据时的成本，并保证读写过程稳定、可恢复、可追踪。

## 2. 内存优化是什么

内存优化关注程序运行时占用的 RAM、峰值 RSS、对象分配和释放。

在这个项目里，内存压力主要来自：

- PDF 页面渲染后的 bitmap；
- OpenCV `cv::Mat` 图像和中间预处理结果；
- ONNX Runtime model session；
- OCR/Layout/Table 输入 tensor 和输出 tensor；
- OCR 检测框、裁剪图、识别 batch；
- Pipeline 中保存的 page texts、layout blocks、tables、reading order、debug artifacts；
- Worker 常驻进程中的 engine/model cache。

内存优化的目标通常是：

- 降低峰值内存；
- 减少重复加载模型；
- 减少大对象复制；
- 控制 cache 容量；
- 让临时对象尽早释放；
- 根据业务设置页数、batch size、DPI 等上限。

一句话说：

> 内存优化就是控制程序在处理大文档、大图片、大模型时的峰值占用，并尽量复用昂贵资源、减少无意义拷贝。

## 3. 做优化前先想什么

不要一上来就改代码。一般先判断瓶颈类型：

```text
慢在磁盘/网络等待 -> 偏 I/O 优化
慢在 CPU 图像处理/后处理 -> 偏算法或并行优化
慢在模型推理 -> 偏模型、batch、session、硬件优化
容易 OOM -> 偏内存峰值、缓存、数据生命周期优化
偶发读到坏文件 -> 偏原子写、事务、发布顺序优化
```

这个仓库里可以按文档解析流程拆：

```text
打开 PDF
  -> 渲染页面 PNG
  -> 原生文本提取 / OCR 读图
  -> Layout / Table 读图和推理
  -> Assembly 保存中间结果
  -> Export 写 JSON/Markdown/HTML
  -> Worker 发布 event/artifact
```

每一段都可能有 I/O 或内存问题。

## 4. 常见 I/O 优化方法

### 4.1 流式读写，避免一次读完整文件

适用场景：

- 大 PDF；
- 大模型文件；
- 大 tar 包；
- 大 JSONL / 日志；
- 下载文件。

典型做法：

- 固定大小 buffer 分块读取；
- 读取时顺便计算 hash；
- 下载时边读边写，不先把 response 全部放进内存；
- 压缩包打包时流式写入。

### 4.2 缓存已验证的数据

适用场景：

- 模型文件；
- 依赖包；
- 预处理后的结果；
- 已加载的 model session。

典型做法：

- 文件存在且 SHA256 匹配就复用；
- 损坏或 hash 不一致才重新下载；
- 常驻 Worker 复用 `DocumentEngine`；
- cache 必须有容量上限。

### 4.3 原子写入

适用场景：

- Worker 写 artifact manifest；
- model metadata；
- 运行状态文件；
- 多进程共享文件。

典型做法：

```text
先写 destination.tmp
flush / close
rename destination.tmp -> destination
失败时删除 tmp
```

这样读者要么看到旧文件，要么看到完整新文件，不会看到半截 JSON。

### 4.4 减少重复 I/O

适用场景：

- 同一页面被 OCR、Layout、Table 多次读图；
- 同一模型每个 job 都重新加载；
- 同一输出被多次序列化。

可选做法：

- 在内存里传递 `cv::Mat` 或 page image cache；
- 在 Worker 中复用 engine/model session；
- 按配置 key 缓存已初始化对象；
- 对 debug artifact 做开关，默认不写大量中间文件。

### 4.5 控制长期增长

适用场景：

- Redis Streams；
- event log；
- runtime artifacts；
- debug 输出目录；
- Docker volume。

典型做法：

- stream 设置 `MAXLEN`；
- run 设置 TTL；
- 定期清理 runtime；
- debug artifact 默认关闭；
- artifact 和 metadata 分开管理。

## 5. 常见内存优化方法

### 5.1 复用重资源

最典型的是模型 session。

模型加载通常很重，包括：

- 读取 ONNX 文件；
- 构建推理 session；
- 初始化内部 graph；
- 分配 session 内部 buffer。

如果每解析一个文档都重新加载模型，延迟和内存抖动都会很大。更合理的是：

- 一个 engine 初始化一次模型；
- 多次 parse 复用同一批 session；
- 并发解析时创建多个 engine，而不是同一个 engine 并发进入 backend。

### 5.2 控制 cache 容量

缓存不是越多越好。模型 session 很大，如果所有 backend 组合都缓存，Worker 会占用大量内存。

所以 cache 要考虑：

- key 怎么定义；
- 命中后怎么更新热度；
- 超过容量怎么淘汰；
- 初始化失败的对象不能进 cache；
- 容量要能配置。

### 5.3 控制输入尺寸

文档智能里图像尺寸非常关键。

比如 200 DPI 的 A4 页面转成 RGBA bitmap，会有几百万像素；如果再转 float tensor，内存会放大很多。常见方法：

- 限制渲染 DPI；
- 模型输入 resize 到固定尺寸；
- OCR detection 设置 `limit_side`；
- recognition crop 限制最大宽度；
- Table/Layout 模型使用固定 input size；
- 超大文档限制最大页数。

### 5.4 batch size 可控

Batch 可以提升吞吐，但也会增加峰值内存。

例如 OCR recognition：

- batch 大，推理次数少，但 tensor 更大；
- batch 小，峰值内存低，但推理次数更多。

所以 batch size 应该变成配置项，而不是写死。

### 5.5 减少拷贝和重复分配

C++ 中常见方法：

- `reserve()` 提前分配 vector 容量；
- `std::move()` 转移大对象；
- `unique_ptr` 管理大资源；
- 函数返回时避免不必要复制；
- 临时对象放在较小作用域里，让析构尽早发生。

### 5.6 分阶段释放和流式 Pipeline

最理想的内存模型是 page-by-page 或 window-by-window：

```text
render page N
  -> OCR/Layout/Table page N
  -> assembly page N
  -> release page N intermediate data
```

但如果最终需要跨页去重、跨页表格延续、全局阅读顺序，就不能简单每页立即丢弃。工程上要做取舍。

## 6. 仓库里已经用到的 I/O 优化

### 6.1 源文件 SHA256 用固定 buffer 流式读取

代码位置：

- `cpp/common/file_fingerprint.cpp`

做法：

`fingerprintFile()` 没有把整个 PDF 一次读入内存，而是用固定大小 buffer 分块读取：

```cpp
std::array<char, 64 * 1024> buffer{};
while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count > 0) {
        digest.update(...);
        fingerprint.size_bytes += ...;
    }
}
```

优化点：

- I/O 是顺序读取，适合大文件；
- 内存占用固定为 64KB 级别；
- 读文件时顺便计算 size 和 SHA256；
- 输出结果可以写入 Document Contract，支持可追踪性。

面试说法：

> 源文件 fingerprint 没有 read_all，而是 64KB buffer 流式读取，同时统计 size 和 SHA256。这样大 PDF 也不会因为计算 hash 占用额外大内存。

### 6.2 模型文件同步时边下载边校验

代码位置：

- `scripts/package_model_pack.py`
- `scripts/setup_model_pack.sh`

做法：

`download_verified_file()` 下载模型时按 1MB chunk 读取：

```python
with urllib.request.urlopen(request, timeout=120) as response:
    with temporary.open("wb") as output:
        for chunk in iter(lambda: response.read(1024 * 1024), b""):
            output.write(chunk)
            digest.update(chunk)
```

下载完以后比对 SHA256，匹配后才 `os.replace(temporary, destination)`。

优化点：

- 下载时不把模型完整读入内存；
- 写临时文件，避免损坏文件覆盖正常模型；
- 边下载边算 SHA256，避免第二次完整扫描；
- 已存在且 hash 正确的文件直接复用；
- hash 不一致才重新下载。

`setup_model_pack.sh` 安装 metadata 时也先写临时文件再 `mv -f`：

```bash
cp "$source" "$temporary"
chmod 0644 "$temporary"
mv -f "$temporary" "$destination"
```

面试说法：

> 模型包同步这块做了 I/O 和可靠性优化：模型按 chunk 下载，边写边算 SHA256；通过校验后再 replace 到目标路径。已有模型如果 hash 对，就不重复下载。

### 6.3 Worker artifact manifest 原子发布

代码位置：

- `platform/worker/worker_stage_observer.cpp`
- `platform/worker/worker_stage_observer_test.cpp`

做法：

`writeJsonAtomically()` 先写 `.tmp` 文件：

```cpp
std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
output << value.dump(2) << '\\n';
output.flush();
output.close();
std::filesystem::rename(temporary, destination, rename_error);
```

失败时删除临时文件。

优化点：

- 避免 API/前端读到半写入 JSON；
- 多进程共享 artifact 目录时更安全；
- 写 manifest 和发布 `artifact_ready` 事件的顺序更可靠。

这不一定减少 I/O 次数，但提高了 I/O 正确性。面试里可以说这是 I/O 可靠性优化。

面试说法：

> Worker 写 artifact manifest 时不是直接覆盖目标文件，而是先写临时文件，flush/close 后 rename。这样 API 要么读到旧文件，要么读到完整新文件，不会读到半截 JSON。

### 6.4 Redis Streams 设置长度上限和 TTL

代码位置：

- `platform/deploy/docker-compose.yml`
- `platform/worker/worker_stage_observer.cpp`
- `platform/README.md`

配置里有：

```yaml
RUN_EVENT_STREAM_MAX_LENGTH: "2000"
PLATFORM_EVENT_STREAM_MAX_LENGTH: "100000"
RUN_RETENTION_SECONDS: "604800"
```

Worker 发布事件时使用：

- `redis_.addEvent(..., run_event_stream_maximum_length_)`
- `redis_.expire(event_stream_, run_retention_seconds_)`
- `redis_.expire(run_key, run_retention_seconds_)`

优化点：

- 避免 Redis Stream 无限增长；
- run 状态设置过期时间；
- 平台事件和单 run 事件分开控制上限；
- 这是 I/O 存储压力和内存压力一起控制。

面试说法：

> 平台事件不是无限写 Redis，run-events 和 platform-events 都有 MAXLEN，run key 也有 TTL。这样长时间跑 Worker 时，事件流不会无限占 Redis 内存。

### 6.5 Worker 镜像保持 model-free，模型通过 volume 缓存

代码位置：

- `platform/deploy/docker-compose.yml`
- `platform/deploy/model-init.Dockerfile`
- `platform/deploy/run-model-init.sh`
- `scripts/setup_model_pack.sh`
- `tests/check_worker_container_model_boundary.sh`

做法：

- Worker 镜像不内置模型；
- `model-init` 服务先准备模型；
- 模型目录挂载成 volume；
- Worker 以只读方式挂载 `/models`；
- 模型损坏时由 setup/sync 逻辑修复。

优化点：

- 程序镜像变小；
- 程序升级不需要重新打包模型层；
- 模型可以跨容器重启复用；
- 模型完整性在 Worker 启动前校验。

面试说法：

> 平台部署上把模型 I/O 从 Worker 镜像里拆出来。模型放在持久化目录，由 model-init 负责校验和准备，Worker 只读挂载。这样镜像发布和模型缓存解耦。

### 6.6 C ABI 返回内存 JSON，避免跨语言临时文件

代码位置：

- `sdk/c/src/document_intelligence_engine.cpp`
- `docs/sdk-lifecycle.md`
- `docs/c-api-v1.md`

做法：

C ABI 的 `die_engine_parse()` 返回 `die_document_t`，调用方通过：

- `die_document_json()`
- `die_document_json_size()`

读取 owned UTF-8 JSON。

优化点：

- 跨语言绑定不需要通过临时 JSON 文件传结果；
- 少一次磁盘写入和读取；
- 生命周期由 `die_document_destroy()` 明确管理；
- 对 JNI 等调用更直接。

面试说法：

> C ABI 没有要求绑定层从临时文件读 JSON，而是直接返回 owned document handle，里面保存 UTF-8 JSON。这样跨语言调用少一次文件 I/O，也更容易管理生命周期。

## 7. 仓库里已经用到的内存优化

### 7.1 `DocumentEngine` 复用模型 session

代码位置：

- `sdk/cpp/src/document_engine.cpp`
- `docs/sdk-lifecycle.md`
- `cpp/pipeline/pipeline_service_factory.cpp`

做法：

`DocumentEngine::Impl` 保存：

```cpp
EngineConfig config;
PipelineServiceCreationResult creation;
std::atomic<bool> parsing{false};
```

`creation` 里包含已经创建好的 Pipeline services，也就是各类 backend/model session。

`parse()` 时复用这些 services：

```cpp
result.status = pipeline.parse(
    pipeline_options,
    impl_->creation.services,
    impl_->creation.provenance,
    ...
);
```

优化点：

- 避免每个文档重新加载模型；
- 避免重复初始化 ONNX Runtime session；
- 对服务型场景比 CLI 每次进程启动更友好；
- 单 engine 用 atomic guard 防止并发进入非线程安全 backend。

面试说法：

> 这个项目里内存和性能最关键的一点是 DocumentEngine 复用模型 session。模型只在 engine 初始化时加载，后续 sequential parse 复用同一批 backend services。

### 7.2 Worker 使用有界 LRU engine cache

代码位置：

- `platform/worker/document_engine_cache.cpp`
- `platform/worker/document_engine_cache_test.cpp`
- `platform/worker/worker_document_processor.cpp`
- `platform/deploy/docker-compose.yml`

做法：

Worker 按 backend tuple 缓存 `DocumentEngine`：

```text
document + ocr + layout + table + registry_config
```

命中后：

```cpp
entries_.splice(entries_.begin(), entries_, entry);
```

超容量后：

```cpp
entries_.pop_back();
```

配置里默认：

```yaml
WORKER_ENGINE_CACHE_SIZE: "2"
```

优化点：

- 同一 backend 组合复用模型 session；
- 不同 backend 组合之间有限缓存；
- 最近使用的 engine 保留；
- 最久不用的 engine 淘汰释放内存；
- 初始化失败的 engine 不进入 cache，避免污染缓存。

面试说法：

> Worker 里不是无限缓存模型，而是按 backend 配置做有界 LRU cache。默认容量是 2，命中就复用 engine，超过容量就淘汰最久未使用的 engine。

### 7.3 失败 engine 不进入 cache

代码位置：

- `platform/worker/document_engine_cache.cpp`
- `platform/worker/document_engine_cache_test.cpp`

做法：

创建 engine 后先检查：

```cpp
if (!engine->isReady()) {
    const common::Status status = engine->initializationStatus();
    return {nullptr, status, false};
}
```

只有 ready engine 才会插入 LRU。

优化点：

- 错误配置不会挤掉已有正常 engine；
- 不会缓存一个不可用对象；
- 减少后续重复命中坏对象导致的异常路径；
- 内存 cache 更干净。

面试说法：

> engine cache 后面专门修过一个问题：失败初始化的 engine 不应该进 cache，也不应该触发 eviction。所以现在只有 isReady 的 engine 才会进入 LRU。

### 7.4 ONNX 模型 session 放在 Backend 对象里复用

代码位置：

- `cpp/ocr/paddle_ocr_onnx_backend.cpp`
- `cpp/ocr/paddle_ocr_onnx_backend.h`
- `cpp/layout/doclaynet_onnx_backend.cpp`
- `cpp/layout/paddle_doclayout_onnx_backend.cpp`
- `cpp/table/table_transformer_onnx_backend.cpp`

以 PaddleOCR 为例：

```cpp
struct PaddleOcrOnnxBackend::ModelBundle {
    Ort::Env env;
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> detection_session;
    std::unique_ptr<Ort::Session> recognition_session;
    ...
};
```

Backend 持有：

```cpp
std::unique_ptr<ModelBundle> model_;
```

优化点：

- detection/recognition session 只创建一次；
- tensor name、input shape、dictionary 只加载一次；
- 后续 page OCR 直接复用 session；
- `unique_ptr` 明确 ownership，释放时跟 backend 生命周期绑定。

面试说法：

> PaddleOCR backend 里把 detection session、recognition session、字典和 tensor name 都放在 ModelBundle 里，随 backend 生命周期复用，不会每页重新加载模型。

### 7.5 OCR recognition batch 可配置，并按宽高比排序减少 padding

代码位置：

- `cpp/ocr/paddle_ocr_onnx_backend.cpp`
- `sdk/cpp/include/document_intelligence_engine/engine_config.h`
- `tests/worker_environment_config_test.py`

做法：

OCR detection 得到文本框后，需要裁剪出多个 crop 做 recognition。代码里：

- 先把 crop index 按宽高比排序；
- 根据模型 fixed batch 或 `config.recognition_batch_size` 决定 batch capacity；
- 动态计算 batch 的 `target_width`；
- 对固定 batch 模型，只 pad 到固定 batch，但只解码真实 batch 部分。

关键逻辑：

```cpp
std::stable_sort(sorted_indices.begin(), sorted_indices.end(), ... width_ratio ...);

const std::size_t batch_capacity =
    fixed_batch > 0 ? static_cast<std::size_t>(fixed_batch)
                    : std::max<std::size_t>(1, config.recognition_batch_size);
```

优化点：

- batch 减少 recognition session 调用次数；
- batch size 可配置，能在吞吐和内存之间取舍；
- 按宽高比排序可以减少同 batch 内因为长文本导致的 padding 浪费；
- `recognition_max_width`、`recognition_width_multiple` 控制 tensor 宽度上限和对齐。

面试说法：

> OCR recognition 不是一张 crop 跑一次，而是按 batch 跑。为了控制内存，batch size 是配置项；为了减少 padding，crop 会按宽高比排序后组 batch。

### 7.6 模型输入 resize 和候选数量上限

代码位置：

- `cpp/ocr/paddle_ocr_onnx_backend.cpp`
- `cpp/layout/doclaynet_onnx_backend.cpp`
- `cpp/layout/paddle_doclayout_onnx_backend.cpp`
- `cpp/table/table_transformer_onnx_backend.cpp`
- `sdk/cpp/include/document_intelligence_engine/engine_config.h`

做法示例：

PaddleOCR detection：

- 根据 `detection_limit_side` 限制长边；
- resize 到 32 的倍数；
- `detection_max_candidates` 限制 contour 候选数量。

Layout/Table：

- DocLayNet、Paddle Layout、Table Transformer 都会把页面图像 resize 到模型输入尺寸；
- tensor size 由配置中的 input width/height 控制。

优化点：

- 控制 tensor 大小；
- 控制候选框后处理成本；
- 避免超大页面直接进入模型；
- 在准确率和内存/耗时之间做配置化取舍。

面试说法：

> 图像模型输入都不能无限大，所以 OCR detection 有 limit side，Layout/Table 会 resize 到模型输入尺寸，OCR 还限制 max candidates。这些都是控制推理内存和后处理成本的手段。

### 7.7 `reserve()` 和 `std::move()` 减少容器扩容和大对象复制

代码位置举例：

- `cpp/document_source/pdf/pdfium/pdf_page_renderer.cpp`
- `cpp/assembly/document_assembler.cpp`
- `cpp/ocr/paddle_ocr_onnx_backend.cpp`
- `cpp/table/table_transformer_onnx_backend.cpp`

例子：

PDF 渲染前预留页数：

```cpp
pages.reserve(static_cast<std::size_t>(page_count));
```

Document assembly 前预留 pages/artifacts：

```cpp
document.pages.reserve(request.pages.size());
artifacts.pages.reserve(request.pages.size());
```

OCR result 预留检测框数量：

```cpp
result.regions.reserve(boxes.size());
```

大量对象 push 时使用 `std::move()`，例如 table rows/cells、blocks、engine config 等。

优化点：

- 减少 vector 多次扩容；
- 减少大对象复制；
- 对页数、block、cell 数量可预估的场景很有效；
- 是 C++ 工程里低风险的内存和性能优化。

面试说法：

> 代码里对页数、artifact、OCR regions、table rows 等可预估容器会先 reserve，再用 move 转移大对象，减少扩容和复制。

### 7.8 最大页数和超时是资源保护

代码位置：

- `cpp/pipeline/document_pipeline.cpp`
- `sdk/cpp/include/document_intelligence_engine/options.h`
- `docs/c-api-v1.md`

做法：

Pipeline 打开文档后会检查 `maximum_pages`：

```cpp
if (options.maximum_pages > 0 && document.source->pageCount() > options.maximum_pages) {
    return stageFailed(... "maximum_pages_exceeded" ...);
}
```

`timeout_seconds` 会在阶段之间检查。

优化点：

- 防止超大 PDF 把 Worker 长时间占住；
- 防止页数过多导致内存持续增长；
- 对 API/Worker 场景是资源保护和调度保护。

需要诚实说明：

当前 timeout 是 cooperative deadline，只在 stage 之间检查，不会强行中断已经进入的模型推理调用。

面试说法：

> maximum_pages 和 timeout_seconds 不只是功能参数，也是资源保护。大文档可以在 open 阶段被拒绝，timeout 则在 stage 边界检查，避免 Worker 被异常任务长期占用。

## 8. 当前仓库里的 I/O 和内存取舍

### 8.1 页面图像写成 PNG artifact，方便检查，但会带来重复读图

当前做法：

- PDF 渲染阶段把每页写成 `pages/page_N.png`；
- OCR/Layout/Table 后端很多时候通过 `cv::imread(page.output_path)` 再读取图像；
- 这样 debug 和 artifact 检查很方便，平台也能展示页面图像。

优点：

- 每个 stage 可以通过文件路径解耦；
- CLI 输出直观；
- 平台 Inspector 可以直接引用 page image；
- 崩溃后仍保留中间页面图像，方便排查。

代价：

- OCR/Layout/Table 可能重复读同一张页面 PNG；
- PNG 编码/解码有 CPU 和 I/O 成本；
- 大文档会产生较多页面图片。

后续可优化：

- Pipeline 内部传递 `cv::Mat` 或 page image cache；
- artifact 写盘和模型推理输入分开；
- 默认只保留最终需要的页面图，debug 才保留更多中间图；
- 对大文档做 page window，处理完一页释放图像内存。

面试说法：

> 当前页面图像落盘是为了 artifact 可检查和平台展示，但代价是后续 OCR/Layout/Table 会重复 imread。后续可以做 page image cache 或内存传递，减少重复 PNG decode。

### 8.2 Pipeline 目前不是完全流式，整份文档中间结果会保留到 Assembly

当前做法：

`DocumentPipeline` 会依次得到：

- `rendered_pages`
- `page_texts`
- `page_layouts`
- `page_tables`
- `page_reading_orders`

然后统一交给 `DocumentAssembler` 组装。

优点：

- 逻辑简单；
- Assembly 可以看到完整文档；
- 方便做跨页处理、header/footer 去重、table continuation、debug artifact；
- 评测和导出更直接。

代价：

- 大文档峰值内存会随页数增长；
- OCR/Layout/Table 中间结果都会保留到最后；
- 如果 debug 信息很多，`PipelineArtifacts` 会更大。

后续可优化：

- page-by-page assembly；
- 分页 flush artifact；
- 对只需要最终 JSON 的场景减少 debug artifact 保留；
- 跨页逻辑只保留必要 summary，而不是所有中间对象；
- 大文档使用 sliding window。

面试说法：

> 当前 Pipeline 为了组装和调试，会把整份文档的中间结果保留到 Assembly，这不是最省内存的做法。后续大文档优化方向是 page window 或 streaming assembly，只保留跨页判断需要的少量状态。

### 8.3 Debug artifact 是可检查性和 I/O 成本之间的取舍

当前做法：

- 普通解析输出 `document.json`、`document.md`、`document.html`、page images；
- `--debug` 时会额外写 debug 信息和预处理图片；
- JSON exporter 也会带更多 stage artifact。

优点：

- 很容易定位问题；
- 面向模型调参和评测很有用。

代价：

- 文件更多；
- JSON 更大；
- I/O 和磁盘占用更高。

后续可优化：

- debug 分级；
- 只保留失败页面 debug；
- artifact retention policy；
- 大对象放外部文件，JSON 只保留 URI。

## 9. 可以继续优化的方向

这些是可以说“我会怎么做”的方向，不要说成仓库已经完成。

### 9.1 减少重复 image decode

现状：

OCR/Layout/Table 会从 page PNG 路径读取图像。

优化方案：

- Render stage 输出 `PageImageHandle`；
- 内部保存 `cv::Mat` 或共享图像 buffer；
- stage 之间传只读引用；
- artifact 写盘异步或延后；
- 大文档使用 LRU page image cache。

风险：

- 内存会增加，需要控制 cache 容量；
- `cv::Mat` 生命周期要清楚；
- 平台 artifact 仍需要文件路径。

### 9.2 Page streaming / windowed Pipeline

现状：

整份文档中间结果保留到 Assembly。

优化方案：

- 每页处理完先生成 page-level blocks；
- 只保留跨页 table/header/footer 需要的 summary；
- 最终导出时流式写 blocks；
- debug artifact 逐页 flush。

风险：

- 跨页表格、页眉页脚去重、全局关系会更复杂；
- JSON 如果需要全局 relations，仍要保留部分索引。

### 9.3 输出 JSON 流式写入

现状：

JSON exporter 通常会构造完整 JSON 再写出。

优化方案：

- 对大文档 blocks 流式写；
- pages、blocks、warnings 分段输出；
- C ABI 仍需要返回完整 JSON 时，可以保留当前方式；
- CLI/file export 可以优先流式。

风险：

- 流式 JSON 生成更难维护；
- 出错后的原子性需要配合临时文件。

### 9.4 更细的内存指标

现状：

评测文档里定义了 `peak_rss_mib_p50/p95`，但完整 Product 层 runner 还没接入。

优化方案：

- benchmark runner 记录 peak RSS；
- 按页归一化；
- 区分冷启动和热启动；
- 记录 backend 配置、DPI、batch size；
- 把结果写入 Quality Report v1 的 product layer。

面试说法：

> 现在仓库已经有质量报告框架，但 Product 层的 peak RSS/p95 latency 还没完整接入。后续我会把它放进 end-to-end runner，跟文本质量指标一起追踪。

### 9.5 模型 session 内存分级

现状：

Worker cache 按 backend tuple 缓存完整 `DocumentEngine`。

优化方案：

- 对 OCR/Layout/Table session 分开缓存；
- 热 backend 常驻，冷 backend 延迟加载；
- 根据模型大小设置不同权重，而不是简单按 engine 数量；
- 支持内存压力下主动 evict。

风险：

- session 生命周期和线程安全会更复杂；
- backend 组合 provenance 需要继续保持清晰。

## 10. 面试中怎么讲

### 10.1 一分钟版本

可以这样说：

> I/O 优化主要是减少磁盘、网络、Redis 等外部读写成本，并保证写入可靠；内存优化主要是降低大图片、大 tensor、大模型 session 带来的峰值内存。这个项目里已经做了一些实际优化，比如源文件 SHA256 用 64KB buffer 流式读取，模型下载按 1MB chunk 边写边校验，artifact manifest 用临时文件加 rename 原子发布，Worker 的 Redis Stream 有 MAXLEN 和 TTL。内存上，`DocumentEngine` 会复用模型 session，Worker 用有界 LRU cache 复用 engine，OCR recognition batch size 可配置，模型输入会 resize 并限制候选数量，代码里也大量使用 `reserve` 和 `move` 降低拷贝。当前还没完全做到的是 page-level streaming，Pipeline 仍会保留整份文档中间结果，这是后续大文档内存优化方向。

### 10.2 三分钟版本

可以这样说：

> 我会先区分 I/O 和内存。I/O 关注读写外部系统，比如 PDF 文件、模型下载、artifact JSON、Redis event；内存关注大对象生命周期，比如 PDF bitmap、OpenCV Mat、ONNX tensor、model session 和 Pipeline artifacts。这个项目里 I/O 上做了几个实际点：第一，源文件 fingerprint 是 64KB buffer 流式读，不会把 PDF 整体读入内存；第二，模型包同步是 chunk 下载，边写边算 SHA256，校验通过后才 replace；第三，Worker 写 artifact manifest 是先写 `.tmp` 再 rename，避免 API 读到半写入 JSON；第四，Redis event stream 设置 MAXLEN 和 TTL，防止长期运行无限增长。\n+\n+> 内存上，最重要的是模型 session 复用。`DocumentEngine` 初始化时创建 backend services，后续 parse 复用；Worker 又在外层做了按 backend tuple 的有界 LRU engine cache，默认容量 2，失败 engine 不进缓存。模型推理上，PaddleOCR backend 把 detection/recognition session、tensor name、dictionary 放进 ModelBundle；recognition crop 按宽高比排序后 batch，batch size 可配置，减少推理次数和 padding 浪费。图像模型输入也会 resize，OCR detection 有 limit side 和 max candidates，避免超大 tensor 和过多候选。\n+\n+> 但我也会明确说当前取舍：为了 artifact 可检查，页面会写成 PNG，后续 OCR/Layout/Table 又从路径读图，所以有重复 image decode；Pipeline 现在也不是完全流式，会把整份文档的中间结果保留到 Assembly。后续如果做大文档优化，我会优先做 page image cache、page-window pipeline、流式 JSON export 和 peak RSS 指标接入 Quality Report。

## 11. 简历/面试可以提的关键词

可以提：

- 流式 SHA256；
- chunk download；
- verified model cache；
- atomic artifact publish；
- Redis Streams MAXLEN / TTL；
- model-free Worker image；
- reusable `DocumentEngine`；
- bounded LRU engine cache；
- ONNX session reuse；
- configurable OCR batch size；
- input resize / candidate cap；
- `reserve` / `std::move`；
- maximum pages / cooperative timeout；
- page streaming 是后续优化方向。

不建议吹：

- 不要说 Pipeline 已经完全流式；
- 不要说已经完整接入 peak RSS p95；
- 不要说完全避免重复读图；
- 不要说 cache 可以无限提升性能，必须提容量和内存取舍；
- 不要说 timeout 能强杀模型推理，目前是 stage 边界检查。
