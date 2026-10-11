# Thread Scheduling

Thread Scheduling 是一个面向 Android Root 环境的线程 CPU 亲和性管理原型。它使用 C++ native daemon 按用户规则发现应用线程，将线程绑定到指定 CPU 核心，并持续记录线程 CPU 时间、最近运行的核心和核心频率。

当前仓库提供的是 `threadschedd` C++17 守护进程和设计文档，尚未打包成可直接安装的 Magisk、KernelSU 或 APatch ZIP 模块。需要 `module.prop`、`service.sh`、Android NDK 打包流程和设备端启动集成后，才能作为完整 Root 模块刷入。

## 功能

- 按 `applist.json` 中的包名和线程名匹配规则。
- 使用 `sched_setaffinity` 设置线程允许运行的 CPU 核心集合。
- 周期性扫描 `/proc/<pid>/task`，发现应用新线程。
- 记录线程 CPU 时间增量、最近运行的 CPU、当前核心频率和绑定结果。
- 将采样结果追加写入 JSONL 文件。
- 支持 `scan`、`single` 和 `event_driven` 三种绑定模式。

## 重要限制

### 功耗不是线程级实测值

Android 通常不提供每个线程的真实功耗接口。当前版本没有接入设备能耗计数器，因此 `power_source` 为 `unavailable`，`power_estimate_uw` 为 `null`，不会伪造功耗数值。

要得到可解释的功耗数据，需要针对目标 SoC 接入 per-core/per-cluster energy counter，并建立校准模型。只有整机或电源轨计数器时，只能报告设备级或电源轨级数据，不能归因给单个线程。

### 当前是 daemon 原型

当前版本还没有：

- Root 模块安装包
- `module.prop` 和 `service.sh`
- 配置热加载和控制 socket
- `threadschedctl` CLI
- SQLite 存储和查询界面
- Android PackageManager 级别的包名解析；当前通过 `/proc` 进程命令行匹配包名
- 设备级功耗计数器适配

## 目录结构

```text
applist.json                    # 应用/线程绑核规则示例
native/                         # C++ native daemon 工程
├── CMakeLists.txt              # CMake 构建配置、编译选项和 Android log 库链接
├── third_party/nlohmann/       # MIT 许可的单头文件 JSON 解析库
│   ├── json.hpp                # nlohmann::json 单头文件实现
│   └── LICENSE.MIT             # 第三方库许可证
├── include/                    # 公共头文件和数据结构声明
│   ├── config.h                # 配置加载器接口和规则集合
│   ├── core_topology.h         # CPU 性能簇自动识别和核心选择解析
│   ├── cpu_binder.h            # CPU affinity 绑定接口
│   ├── event_watcher.h         # proc connector 事件监听接口
│   ├── model.h                 # 规则、线程信息和采样记录的数据模型
│   ├── proc_reader.h           # /proc 进程与线程信息读取接口
│   └── sampler.h               # CPU 时间、频率采样和 JSONL 输出接口
└── src/                        # C++ 实现文件
    ├── config.cpp              # 解析 applist.json 中的包名、线程、核心选择和模式
    ├── core_topology.cpp       # 读取 CPU capacity/最大频率并映射 e/p/hp-core
    ├── cpu_binder.cpp          # 构造 cpu_set_t 并调用 sched_setaffinity
    ├── event_watcher.cpp       # 监听线程/进程创建事件并提供降级状态
    ├── main.cpp                # daemon 入口、主循环和规则调度
    ├── proc_reader.cpp         # 扫描 /proc、枚举 PID/TID 并读取线程 stat/comm
    └── sampler.cpp             # 读取核心频率、计算 CPU 时间增量并追加 JSONL
```

数据流为：`main.cpp` 加载 `config.cpp` 的规则，`proc_reader.cpp` 发现目标线程，`cpu_binder.cpp` 执行绑核，`sampler.cpp` 记录采样结果；`model.h` 在各模块之间传递统一的数据结构。

## 配置

守护进程读取独立的 `applist.json`。顶层键是 Android 包名，每个包对应一个规则对象。对象键使用英文：

```json
{
  "com.example.app": {
    "thread_name": "RenderThread",
    "cpu_cores": ["0-6", "e-core", "p-core", "hp-core", 7],
    "sample_interval_ms": 1000
  }
}
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| 顶层包名 | 目标 Android 包名，例如 `com.example.app`。daemon 根据 `/proc` 进程命令行匹配包名。 |
| `thread_name` | `/proc/<pid>/task/<tid>/comm` 中的线程名；使用 `*` 匹配该包的所有线程。 |
| `cpu_cores` | 数字或数字字符串表示单个核心；`"0-6"` 表示范围；`"e-core"`、`"p-core"`、`"hp-core"` 表示自动识别的核心组；数组元素可以混合，最终取并集。 |
| `sample_interval_ms` | 采样间隔，范围为 50 到 60000 毫秒。 |
| `mode` | 可选，`scan`、`single` 或 `event_driven`；省略时默认为 `scan`。 |

核心类别通过 sysfs 自动识别：优先使用每个 CPU 的 `cpu_capacity`，缺失时使用 `cpuinfo_max_freq`。最低 capacity 组是 `e-core`，最高组是 `hp-core`，中间组是 `p-core`；只有两组的设备没有 `p-core`。如果设备没有可读取的拓扑数据，使用类别别名会报错，数字和范围仍可使用。

### 模式

- `scan`：每个 `sample_interval_ms` 周期扫描匹配线程并重新设置 affinity。适合应用可能主动修改 affinity 或持续创建线程的场景。
- `single`：daemon 启动时扫描一次并绑定当时存在的匹配线程；后续只记录这些线程，不绑定新线程。
- `event_driven`：通过 Linux proc connector 的进程/线程创建事件触发扫描和绑定；事件接口不可用或异常时，每 5 秒扫描兜底。采样仍按 `sample_interval_ms` 进行。

事件驱动依赖目标内核启用 proc connector，Android 厂商内核可能未启用。daemon 会在日志中报告降级状态，不会把降级伪装成纯事件驱动。

## 构建

项目使用 CMake 和 C++17。Android 构建需要 Android NDK；Linux 构建需要 Linux CMake 工具链。

仓库包含 GitHub Actions 工作流 [`build.yml`](.github/workflows/build.yml)。它会在 `main` 推送、Pull Request 或手动触发时构建 Android `arm64-v8a` 版本，并上传 `threadschedd-android-arm64` artifact。artifact 内包含二进制文件和 `SHA256SUMS`。

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
threadschedd <applist.json> <samples.jsonl>
```

在 Android Root shell 中示例：

```sh
su
/data/adb/thread-scheduling/threadschedd \
  /data/adb/thread-scheduling/applist.json \
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
  "power_source": "unavailable",
  "power_estimate_uw": null
}
```

`cpu` 是 `/proc/<pid>/task/<tid>/stat` 报告的最近运行 CPU，不表示整个采样窗口只运行在该核心。`binding_result` 为 `ok`、`not_requested` 或带 errno 信息的 `error:<reason>`。

## 后续模块化工作

要把 daemon 做成完整 Root 模块，还需要补充：

1. `module.prop`、`service.sh`、卸载脚本和 arm64 安装包。
2. Android NDK 交叉编译和模块 ZIP 打包脚本。
3. 使用 PackageManager 或 `/proc/<pid>/status` 正确获取 UID，并处理 PID 复用。
4. Unix domain socket 或受限 CLI，实现配置热加载和查询。
5. CPU online/offline、进程重启、线程销毁和配置冲突处理。
6. 按目标 SoC 接入 energy counter，并区分 `measured`、`estimated` 和 `unavailable`。

详细设计见 [thread_cpu_binding_design.md](thread_cpu_binding_design.md)，Root 模块兼容规范见 [root_module_compatibility_guide.md](root_module_compatibility_guide.md)。

