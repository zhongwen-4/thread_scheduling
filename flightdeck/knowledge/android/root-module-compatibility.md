# Android Root 模块兼容编写 checklist
SUMMARY: Always target the shared Magisk/KernelSU/APatch module subset first; isolate platform-specific extensions behind checks.
READ WHEN: before writing or reviewing an Android Root module

---

通用基础是 `module.prop`、`system/`、`customize.sh`、`service.sh`、`system.prop`、`sepolicy.rule` 和 `disable`/`remove`/`skip_mount` 状态文件。脚本使用 `MODDIR=${0%/*}`，常规启动逻辑优先放进非阻塞的 `service.sh`；`post-fs-data.sh` 只在确有早期执行需求时使用，并避免 `setprop`。

KernelSU 的 `system/` 挂载依赖 metamodule；APatch 使用 OverlayFS，且模块 ZIP 不支持自定义 Recovery 安装；Magisk 原生支持 Zygisk，KernelSU/APatch 不原生支持 Zygisk。KernelSU 用 `KSU=true` 识别环境，APatch 用 `APATCH=true` 识别环境。

来源：

- https://topjohnwu.github.io/Magisk/guides.html
- https://kernelsu.org/guide/module.html
- https://apatch.dev/apm-guide.html
