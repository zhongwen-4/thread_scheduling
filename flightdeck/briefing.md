# Briefing — thread_scheduling

## Conventions

<!-- Project house rules + AI-maintenance preferences, in plain prose.
     e.g. "publishing surface is English", "ask before force-pushing". -->

代码结构要清晰。每次写完代码都提交一次，提交格式为 `[core] 更新 xxx`、`[core] 添加 xxxx`、`[core] 修复 xxx`、`[core] 优化 xxx` 或 `[core] 删除 xxx`；Flightdeck 相关文件使用对应的 `[flightdeck]` 前缀。

当需求存在技术错误、关键前提不成立或无法实现时，必须直接说明无法实现及具体原因，并提出可行的替代方案；对尚未验证的能力必须明确标注验证限制。

当需求模糊不清且存在多个合理实现方向时，先向用户问清楚需要做决定的内容，说明各选项的影响；在关键选择得到确认前，不开始依赖该选择的实现。

## Subscriptions

<!-- one ~/.flightdeck-relative path per line; empty = subscribe to nothing global -->
