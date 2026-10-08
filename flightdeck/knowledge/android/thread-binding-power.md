# Android 线程绑核与功耗记录设计 checklist
SUMMARY: Always implement binding in a rooted native daemon; report per-thread CPU time and frequency samples, while labeling power as measured only for real counters and otherwise estimated or unavailable.
READ WHEN: before implementing CPU affinity, thread sampling, frequency logging, or power reporting

---

Shell-only module logic cannot reliably observe every application thread. Use a native daemon to enumerate `/proc/PID/task`, match package/UID/process/thread rules, and call `sched_setaffinity` on each TID. Recheck in enforce mode because applications can change affinity and newly created threads may appear.

Per-thread CPU time is available from `/proc/PID/task/TID/stat`; the `processor` field is only the most recently observed CPU. Core frequency can be sampled from `scaling_cur_freq` or `cpuinfo_cur_freq`. Android generally lacks true per-thread power data: energy counters may be per-core/cluster/rail/device, so output must distinguish `measured`, `estimated`, and `unavailable`.

The detailed design is in `thread_cpu_binding_design.md`.
