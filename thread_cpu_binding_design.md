# Android 应用线程 CPU 绑定与运行记录设计

## 目标

按用户配置把应用进程中的线程绑定到指定 CPU 核心，并记录线程在哪些核心运行、CPU 时间、采样频率，以及可解释的功耗估算。

## 可行性边界

### 可以可靠实现

- 以 UID、包名、PID、线程名或 TID 匹配规则。
- 对现有线程和新发现的线程调用 `sched_setaffinity(2)`。
- 通过 `/proc/<pid>/task/` 枚举线程，并读取 `/proc/<pid>/task/<tid>/stat` 的线程 CPU 时间。
- 通过 `/proc/<pid>/task/<tid>/stat` 的 processor 字段记录线程最近运行的 CPU。
- 采样 `/sys/devices/system/cpu/cpuN/cpufreq/scaling_cur_freq` 或 `cpuinfo_cur_freq` 记录核心频率。
- 将采样结果写入 SQLite 或按日期滚动的 JSONL/CSV 文件。

### 不能承诺为真实值

- Android 通常没有每个线程的真实瓦特数接口。
- `/sys/class/power_supply`、功耗计数器或 PMIC 节点多为整机/电源轨粒度，不能直接归因到单线程。
- 线程功耗只能做估算：使用 CPU 时间增量、频率、核心类型和设备校准参数计算相对消耗，并明确标记为 estimate。

## 推荐架构

```text
Root 模块
├── service.sh                 # 启动/停止 daemon，等待系统完成启动
├── daemon/threadschedd        # C/C++ native daemon
├── config/default.json        # 默认规则
├── bin/threadschedctl         # 配置和查询 CLI
└── data/                      # 运行时数据库和日志
```

`service.sh` 只负责生命周期和权限，不负责线程扫描。daemon 负责配置解析、进程发现、线程枚举、绑核、采样和持久化。

当前实现使用 `native/` 下的 C++17 CMake 工程，入口为 `threadschedd`。需要 Android NDK 或 Linux CMake 工具链构建；Windows 开发机未安装 CMake 时无法本地编译验证。

## 配置模型

应用规则独立放在 `applist.json`，包名作为顶层键，每个包对应一个规则对象：

```json
{
  "thread_name": "RenderThread",
  "cpu_cores": ["p-core", "4-6", 7],
  "sample_interval_ms": 1000
}
```

`cpu_cores` 支持数字、数字字符串、数字范围和 `e-core`、`p-core`、`hp-core`，数组元素取并集。`mode` 可选，省略时使用 `scan`；`scan` 按采样间隔重新绑定，`single` 只绑定 daemon 启动时的线程快照，`event_driven` 由 proc connector 事件触发并以 5 秒扫描兜底。核心组根据 sysfs CPU capacity 或最大频率自动分组。

## 线程发现与绑定

1. 读取配置并校验 CPU 编号是否在线。
2. 通过 `/proc` 扫描目标 PID，再扫描 `/proc/PID/task/TID/comm` 和 `cmdline`。
3. 对匹配线程调用 `sched_setaffinity(tid, sizeof(cpu_set_t), &mask)`。
4. 新线程会继承创建者的 affinity，但应用可能主动改回，因此 `scan` 模式需要周期性复查；`event_driven` 模式用创建事件触发，并保留低频扫描兜底。
5. PID 复用时必须同时校验 `/proc/PID/cmdline`、UID 和进程启动时间，避免把规则应用到错误进程。

绑定失败要记录 errno、线程身份和目标 CPU，不得静默忽略。线程结束、进程退出和规则删除时清理内存状态。

## 采样字段

每个线程每次采样至少记录：

```text
timestamp_ns
uid, package, pid, tid, thread_name
cpu_before, cpu_after
utime_ticks, stime_ticks
delta_cpu_time_ns
freq_before_hz, freq_after_hz
affinity_mask
binding_result
power_estimate_uw
power_source = measured_cpu_counter | unavailable
```

`/proc/PID/task/TID/stat` 的 CPU 时间使用系统 `_SC_CLK_TCK` 换算为纳秒；processor 字段只表示最近一次运行的 CPU，不代表整个采样窗口只在该 CPU 上运行，因此需要记录前后值并结合 affinity 解读。

## 功耗估算

优先级：

1. 设备暴露 per-core 或 per-cluster energy counter 时，使用增量计数器。
2. 只有电源轨/整机计数器时，只记录 rail/device 级别，不能伪造线程级功耗。
3. 没有计数器时不输出线程级功耗数值，标记为 `unavailable`；接入校准模型后再增加 `estimated` 状态。

输出必须区分 `measured`、`estimated` 和 `unavailable`。当前实现只输出 `unavailable`，避免把没有物理依据的数值展示成测量结果。

## 权限与安全

- daemon 需要 Root 才能跨 UID 枚举线程并调用 `sched_setaffinity`。
- 配置文件放在模块私有目录，修改前校验 JSON、CPU 范围和包名。
- 使用 Unix domain socket 或受限 CLI 通信，校验调用者 UID；不要暴露任意 shell 执行接口。
- 默认只读采样，绑定策略由显式规则启用；无效规则不得影响启动。
- 日志轮转，避免线程采样长期占满 `/data`。

## 分阶段实现

### Phase 1：可用闭环

- native daemon 扫描配置中的包/UID。
- 枚举线程并执行 `sched_setaffinity`。
- 记录绑定成功/失败、CPU 时间和最近 CPU。
- 提供 `threadschedctl validate|list|set|remove`。

### Phase 2：频率与稳定性

- 读取每个核心当前频率。
- 处理进程重启、PID 复用、线程名变化和 CPU hotplug。
- 增加 SQLite 存储、查询和导出。

### Phase 3：功耗

- 探测设备 energy counter 和 power supply 节点。
- 建立按 SoC/核心类型的校准配置。
- 只在有数据源时报告 measured，否则报告 estimated 或 unavailable。

### Phase 4：管理界面

- 用 CLI 或 WebUI 编辑规则、查看线程历史和绑定失败。
- UI 明确显示数据粒度和估算状态。

## 关键测试

- 启停 daemon、模块禁用和卸载不阻塞系统启动。
- 应用冷启动、热启动、分身进程和多进程场景。
- 线程频繁创建/销毁、线程名重复、PID 复用。
- CPU offline/online、大小核和不同刷新频率。
- 规则冲突、非法 CPU、目标包不存在和 daemon 重启恢复。
- 对照 `taskset -p` 和 `/proc/PID/task/TID/status` 的 `Cpus_allowed_list` 验证 affinity。
