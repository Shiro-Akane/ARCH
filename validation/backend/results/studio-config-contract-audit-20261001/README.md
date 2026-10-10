# Core／Studio 配置契约审计：2026-10-01

本目录保存对话“检查 ARCH Studio GUI 状态”的既有 CPU 审计摘要，
用于固定下一轮接口交付的基线。原文件为
`/tmp/arch-studio-contract-audit-20261001/audit-summary.json`；
[summary.json](summary.json) 原样保留该摘要，不补写尚未取得的测试输出。

归档时只复核源文件、当前分支和有关报告，没有重跑测试或编译 GPU。
原审计的 workspace、Node 路径是当时环境身份，不是其他机器的安装要求。

| 证据 | 已记录结果 | 限制 |
| --- | --- | --- |
| Core | main `25adec4224497981a0c124a3485f786194975be4`；CPU 契约 11/11 | 配置、预览和初始化接口范围 |
| 本地 Studio | `840ab538f676f9b898a4680acaabc80201b13647`；170/171 | 本地 profile 调整；一项引用旧 `fallback.json` |
| Host → Core | 95 参数；输入身份匹配；未初始化 CUDA／执行 Setup | 不等于运行就绪；构建依赖覆盖仍 unknown |
| 模型 | 注册 14；完整场／AMR 为 Sod、CellularDet | 能力分类分别查询 |
| 新工作台 | 请求核对 `c96e9da0a6d114fd0323102b73dba4a3cd8da9ba` | 未取得源码，不能独立认证 |
| 候选迁移探针 | 旧校验器拒绝 synthetic null／missing 和新版本 | 仅证明客户端假设，不是已实现的新协议 |

原摘要 `realHost.schemaVersion="2"` 表示配置扩展版本；
[现行协议](../../../../src/api/docs/CONFIGURATION_API.md) 的外层 `schemaVersion` 为 `"1.0"`，
二者不能混用。错误样例替换为非法数值明确失败，不恢复旧静默回填行为。

本目录不是完整原始测试归档，也不替代合作者下一轮提交的日志、源码及人工验收。
执行与报告要求见 [Core／Studio 联合交付计划](../../../../docs/development/StudioConfigurationHandoff.zh-CN.md)。
