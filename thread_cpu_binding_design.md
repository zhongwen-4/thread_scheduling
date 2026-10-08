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

建议每条规则包含：

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

匹配优先级：`uid + process + thread_name` > `package + process` > `uid` > `package`。同一线程命中多条规则时使用最具体的一条。`mode=enforce` 表示周期性纠正 affinity，`mode=once` 只在发现线程时设置一次。

## 线程发现与绑定

1. 读取配置并校验 CPU 编号是否在线。
2. 通过 `/proc` 扫描目标 PID，再扫描 `/proc/PID/task/TID/comm` 和 `cmdline`。
3. 对匹配线程调用 `sched_setaffinity(tid, sizeof(cpu_set_t), &mask)`。
4. 新线程会继承创建者的 affinity，但应用可能主动改回，因此 `enforce` 模式需要周期性复查。
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
power_source = estimated_cpu_model | rail_counter | unavailable
```

`/proc/PID/task/TID/stat` 的 CPU 时间使用系统 `_SC_CLK_TCK` 换算为纳秒；processor 字段只表示最近一次运行的 CPU，不代表整个采样窗口只在该 CPU 上运行，因此需要记录前后值并结合 affinity 解读。

## 功耗估算

优先级：

1. 设备暴露 per-core 或 per-cluster energy counter 时，使用增量计数器。
2. 只有电源轨/整机计数器时，只记录 rail/device 级别，不能伪造线程级功耗。
3. 没有计数器时，用校准模型估算：`energy ~= cpu_time * f(freq, core_type, voltage, calibration)`。

输出必须区分 `measured` 和 `estimated`。默认不把估算瓦特数展示成测量值，也不跨设备比较未校准的数值。

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
