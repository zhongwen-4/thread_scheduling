# 需求可行性澄清 checklist
SUMMARY: Always identify technically incorrect or impossible requirements, state the concrete reason, and propose a feasible alternative with verification limits.
READ WHEN: before implementing a requirement with platform, permission, hardware, or API constraints

---

遇到技术错误、关键前提不成立或无法实现的需求时，先直接说明原因，不把不可验证的行为包装成已实现功能。随后给出可行替代方案，并标注哪些结论已验证、哪些需要目标设备、工具链或运行时测试确认。

需求存在多个合理方向时，先澄清必须由用户决定的范围、目标平台、数据精度、交互方式、兼容性和风险取舍。问题应带上选项及影响；关键选择未确认前，只做不依赖该选择的调查或准备工作。
