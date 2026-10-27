# AI 使用说明 / AI-use disclosure

## v1.4.0 补充

Windows 原生适配期间，AI 辅助用于平台兼容实现、冻结边界候选整理、失败诊断、既有阈值下的回归核对、依赖审阅及文档归档；DeepSeek 用于有界独立复核。数学边界例外由维护者逐项批准，公式、容差、拒绝检查与内存池契约保持。实际执行范围与保留限制见 [Windows 验收记录](../histroy/build/WindowsNativeCudaQualification-20261027.md)和[发布准备记录](../histroy/release/V1.4.0ReleasePreparation-20261027.zh-CN.md)。GitHub 软件发布日期为2026-10-27；Zenodo 仍暂缓。

For v1.4.0, AI assistance covered native Windows compatibility work, proposed frozen-boundary exceptions, failure diagnosis, regression review under existing thresholds, dependency review and documentation. DeepSeek supplied bounded independent reviews. The maintainer approved mathematical exceptions individually; formulas, tolerances, rejection checks and memory-pool contracts remain unchanged. Recorded acceptance scope and limitations continue to govern the claims.

## 既有版本披露

适用版本：ARCH 1.3.0 与 1.3.1 维护版。披露依据：维护者于 2026-10-20 明确确认的使用范围。

AI 辅助用于 bug 追踪与修复、完整 CTest 测试集的容差设置核对、部分文档编辑、代码补全，以及工程与安全检查。软件整体架构由人类贡献者设计并搭建；科学定义、数值验收标准、审查与发布决定由人类维护者负责。

“完整 CTest 容差核对”说明辅助工作的覆盖范围；各测试与科学结论仍以发布版本保存的实际验收记录及其适用范围为准，既有数值门槛保持。工程安全检查按已有检查证据表述，不承诺不存在任何缺陷或漏洞。AI 工具不列为软件作者；作者姓名、顺序和 ORCID 见 [贡献说明](CONTRIBUTIONS.md)。未核实的工具版本、提示词、次数或占比不作补填。

## English disclosure

AI-assisted tools were used for bug tracking and fixes, review of tolerance settings across the complete CTest suite, editing selected documentation, code completion, and engineering and security checks. The overall software architecture was designed and built by human contributors. Human maintainers retain responsibility for scientific definitions, numerical acceptance criteria, code review, and release decisions.

Tolerance review describes the assisted activity; test outcomes and scientific qualifications are those recorded in the release evidence. Engineering and security checks are bounded by their recorded scope.

1.3.1 维护版新增活动限于依赖审阅、发布清理、归档准备及 Windows 计划审阅；没有新增科学实现或重复全套科学验收。
