# Android Root 模块兼容编写规范

本文以 Magisk、KernelSU、KernelSU Next 与 APatch 官方开发文档为依据，定义可跨 Root 管理器复用的 systemless 模块基础，以及必须隔离的平台扩展。

## 1. 兼容基础

| 能力 | Magisk | KernelSU / Next | APatch |
| --- | --- | --- | --- |
| `module.prop`、`customize.sh` | 支持 | 支持 | 支持 |
| `system/` 覆盖 | Magic Mount | 需要 metamodule | OverlayFS |
| `service.sh` | 支持 | 支持 | 支持 |
| `system.prop`、`sepolicy.rule` | 支持 | 支持 | 支持 |
| Zygisk | 原生 | 不原生支持 | 不原生支持 |
| 自定义 Recovery 刷包 | 支持 | 不支持 | 不支持 |

兼容模块应先只使用三者共有能力。Zygisk、WebUI、Lua、KPM、`initrc`、`post-mount.sh` 和 `boot-completed.sh` 应明确标注平台要求。

## 2. 推荐目录

```text
module.zip
├── module.prop
├── customize.sh          # 可选，安装时被 source
├── system/               # 可选，systemless 文件覆盖
├── service.sh            # 常规启动逻辑的首选
├── post-fs-data.sh       # 仅在确需早期执行时使用
├── post-mount.sh         # KernelSU / APatch 扩展
├── boot-completed.sh     # KernelSU / APatch 扩展
├── uninstall.sh          # 卸载时执行
├── action.sh             # 管理器 Action 按钮
├── system.prop
└── sepolicy.rule
```

安装后模块位于 `/data/adb/modules/<id>/`。状态文件 `disable`、`remove`、`skip_mount` 分别表示禁用、下次重启移除和不挂载 `system/`。

## 3. module.prop

```properties
id=example.compat
name=Example Compat Module
version=1.0.0
versionCode=1
author=Your Name
description=Magisk KernelSU APatch compatible module
```

`id` 必须匹配 `^[a-zA-Z][a-zA-Z0-9._-]+$`，发布后不可随意修改；`versionCode` 必须为整数；所有字段单行，文件使用 Unix LF 换行。KernelSU 额外支持 `actionIcon` 和 `webuiIcon`，其他平台应忽略未知字段。

## 4. Shell 脚本

```sh
#!/system/bin/sh
MODDIR=${0%/*}

if [ "$KSU" = "true" ]; then
    ROOT_IMPL=KernelSU
elif [ "$APATCH" = "true" ]; then
    ROOT_IMPL=APatch
else
    ROOT_IMPL=Magisk
fi
```

三者的模块脚本都运行在自带 BusyBox ash 环境中。不要写死 `/data/adb/modules/<id>`，也不要假设系统 BusyBox 的路径和版本。

## 5. 启动时机

- `service.sh`：默认选择，late_start，非阻塞。
- `post-fs-data.sh`：模块挂载前执行且会阻塞启动；只用于确实需要早期执行的逻辑。不要在此阶段使用 `setprop`，需要改属性时使用 `resetprop -n`。
- `post-mount.sh`：KernelSU/APatch 在 OverlayFS 挂载后提供。
- `boot-completed.sh`：KernelSU/APatch 在系统启动完成后提供；Magisk 可在 `service.sh` 中等待 `sys.boot_completed`。

## 6. systemless 文件

文件相对于真实 `/system` 放入 `system/`：

```text
system/vendor/etc/example.conf
system/product/app/example.apk
system/system_ext/etc/example.xml
```

新增文件会被合并，重名文件会覆盖原文件。删除文件可使用 `mknod <name> c 0 0` whiteout，或在 `customize.sh` 中设置 `REMOVE`。整目录替换在 Magisk 使用 `.replace`，在 KernelSU/APatch 的 OverlayFS 中使用 opaque 目录或 `REPLACE`；为提高兼容性，优先覆盖单个文件。

## 7. 安装器

Magisk 可使用 ZIP 直接在 Manager 安装；若还要支持 Recovery，需要 `META-INF/com/google/android/update-binary` 和内容为 `#MAGISK` 的 `updater-script`。KernelSU/APatch Manager 可直接安装模块 ZIP，但官方不支持自定义 Recovery 刷入。

`customize.sh` 会被 source，默认变量包括 `MODPATH`、`TMPDIR`、`ZIPFILE`、`ARCH`、`IS64BIT`、`API`。只有完全接管解压时才设置 `SKIPUNZIP=1`，不要在脚本末尾调用 `exit`。

## 8. 平台扩展

### Magisk

支持 `zygisk/` 原生库、`overlay.d/` boot ramdisk 覆盖、`system.prop` 和 `sepolicy.rule`。

### KernelSU / KernelSU Next

`system/` 需要 metamodule 才能挂载；脚本、属性和 sepolicy 不依赖 metamodule。KernelSU 可提供 WebUI、模块配置、`initrc/`、`post-mount.sh` 和 `boot-completed.sh`。不原生提供 Zygisk，可按其文档使用兼容层。

### APatch

使用 `/data/adb/ap/bin/busybox`，systemless 层基于 OverlayFS；提供 `post-mount.sh`、`boot-completed.sh`、Lua 模块和 KernelPatch 模块。不原生提供 Zygisk，且模块 ZIP 不应设计为 Recovery 刷包。

## 9. 发布检查

- 在 Magisk、KernelSU/Next、APatch 分别安装、启用、禁用、升级和卸载。
- 检查 `module.prop` 的 ID、整数版本号和 LF 换行。
- 检查所有脚本使用 `MODDIR`，没有写死模块路径。
- 在没有 metamodule 的 KernelSU 上确认脚本和属性仍工作，并提示 `system/` 不会挂载。
- 至少测试 arm64；其他架构按模块是否包含 native 库决定。
- 对 Zygisk、WebUI、Lua、KPM、`initrc` 等扩展标注平台要求。
- 保留通过管理器禁用或删除模块的 bootloop 恢复路径。

来源：

- https://topjohnwu.github.io/Magisk/guides.html
- https://kernelsu.org/guide/module.html
- https://apatch.dev/apm-guide.html
