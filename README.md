# Thread Scheduling

Thread Scheduling 是一个面向 Android Root 环境的线程 CPU 亲和性管理原型。它使用 C++ native daemon 按用户规则发现应用线程，将线程绑定到指定 CPU 核心，并持续记录线程 CPU 时间、最近运行的核心和核心频率。

当前仓库提供的是 `threadschedd` C++17 守护进程和设计文档，尚未打包成可直接安装的 Magisk、KernelSU 或 APatch ZIP 模块。需要 `module.prop`、`service.sh`、Android NDK 打包流程和设备端启动集成后，才能作为完整 Root 模块刷入。

## 功能

- 按包名、UID、进程名和线程名匹配规则。
- 使用 `sched_setaffinity` 设置线程允许运行的 CPU 核心集合。
- 周期性扫描 `/proc/<pid>/task`，发现应用新线程。
- 记录线程 CPU 时间增量、最近运行的 CPU、当前核心频率和绑定结果。
- 将采样结果追加写入 JSONL 文件。
- 支持 `enforce` 和 `once` 两种绑定模式配置字段。

## 重要限制

### 功耗不是线程级实测值

Android 通常不提供每个线程的真实功耗接口。当前代码中的 `power_estimate_uw` 只是基于 CPU 时间增量和频率的占位估算，不代表物理测量值，也不能跨设备比较。

要得到可解释的功耗数据，需要针对目标 SoC 接入 per-core/per-cluster energy counter，并建立校准模型。只有整机或电源轨计数器时，只能报告设备级或电源轨级数据，不能归因给单个线程。

### 当前是 daemon 原型

当前版本还没有：

- Root 模块安装包
- `module.prop` 和 `service.sh`
- 配置热加载和控制 socket
- `threadschedctl` CLI
- SQLite 存储和查询界面
- 完整的 Android 包名与 UID 解析
- 设备级功耗计数器适配

## 目录结构

```text
native/
├── CMakeLists.txt
├── include/
│   ├── config.h
│   ├── cpu_binder.h
│   ├── model.h
│   ├── proc_reader.h
│   └── sampler.h
└── src/
    ├── config.cpp
    ├── cpu_binder.cpp
    ├── main.cpp
    ├── proc_reader.cpp
    └── sampler.cpp
```

## 配置

守护进程读取一个包含规则对象的 JSON 文件。当前解析器支持以下字段：

```json
{
  "package": "com.example.app",
  "uid": 10234,
  "process": "com.example.app:worker",
  "thread_name": "RenderThread",
  "cpus": [4, 5],
  "mode": "enforce",
  "sample_ms": 1000
}
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `package` | 目标包名。当前原型通过进程命令行近似匹配，不能替代 Android PackageManager 查询。 |
| `uid` | 目标 UID。当前线程读取路径尚未填充 UID，不能单独依赖该字段。 |
| `process` | 进程命令行匹配值。 |
| `thread_name` | `/proc/<pid>/task/<tid>/comm` 中的线程名。 |
| `cpus` | 要绑定的 CPU 编号列表。 |
| `mode` | `enforce` 表示周期性设置；`once` 表示配置字段语义上的一次性模式。当前主循环仍会扫描规则。 |
| `sample_ms` | 采样间隔，最小实际休眠间隔为 50 ms。 |

可以将多个规则写入数组；当前解析器按 JSON 文本中的对象逐个读取：

```json
[
  {
    "package": "com.example.app",
    "thread_name": "RenderThread",
    "cpus": [4, 5],
    "sample_ms": 1000
  },
  {
    "process": "com.example.app:worker",
    "cpus": [6, 7],
    "sample_ms": 500
  }
]
```

## 构建

项目使用 CMake 和 C++17。Android 构建需要 Android NDK；Linux 构建需要 Linux CMake 工具链。

```bash
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --target threadschedd -j
```

使用 Android NDK 时，把 Android 工具链文件和 ABI 传给 CMake，例如：

```bash
cmake -S native -B native/build \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29
cmake --build native/build --target threadschedd -j
```

将 `android-29` 替换为项目实际支持的 API level。当前开发环境没有安装 CMake/Android NDK，因此仓库尚未完成本机编译验证。

## 运行

`threadschedd` 的命令行格式：

```text
threadschedd <config.json> <samples.jsonl>
```

在 Android Root shell 中示例：

```sh
su
/data/adb/thread-scheduling/threadschedd \
  /data/adb/thread-scheduling/config.json \
  /data/adb/thread-scheduling/samples.jsonl
```

daemon 会持续运行，使用 `Ctrl-C` 或发送终止信号停止。跨 UID 枚举线程和调用 `sched_setaffinity` 通常需要 Root 权限；SELinux 策略、内核配置和厂商限制可能导致绑定失败。

## 输出

每行是一条 JSON 记录，字段示例：

```json
{
  "timestamp_ms": 1720000000000,
  "pid": 1234,
  "tid": 1238,
  "thread_name": "RenderThread",
  "cpu": 4,
  "delta_cpu_time_ns": 12000000,
  "frequency_hz": 1800000000,
  "binding_result": "ok",
  "power_source": "estimated",
  "power_estimate_uw": 21600000
}
```

`cpu` 是 `/proc/<pid>/task/<tid>/stat` 报告的最近运行 CPU，不表示整个采样窗口只运行在该核心。`binding_result` 为 `ok` 或带 errno 信息的 `error:<reason>`。

## 后续模块化工作

要把 daemon 做成完整 Root 模块，还需要补充：

1. `module.prop`、`service.sh`、卸载脚本和 arm64 安装包。
2. Android NDK 交叉编译和模块 ZIP 打包脚本。
3. 使用 PackageManager 或 `/proc/<pid>/status` 正确获取 UID，并处理 PID 复用。
4. Unix domain socket 或受限 CLI，实现配置热加载和查询。
5. CPU online/offline、进程重启、线程销毁和配置冲突处理。
6. 按目标 SoC 接入 energy counter，并区分 `measured`、`estimated` 和 `unavailable`。

详细设计见 [thread_cpu_binding_design.md](thread_cpu_binding_design.md)，Root 模块兼容规范见 [root_module_compatibility_guide.md](root_module_compatibility_guide.md)。

