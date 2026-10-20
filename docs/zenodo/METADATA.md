# ARCH 1.3.0：Zenodo 填写内容

核对日期：2026-10-20。以下固定字段已完成；DOI 与记录 URL 由维护者在自己的 Zenodo 账号中确认或生成。不要把仓库分支名用作版本号。

| 字段 | 填写内容 |
| --- | --- |
| Resource type | Software |
| Title | ARCH: Adaptive Reactive CUDA Hydrodynamics |
| Version | 1.3.0 |
| Publication date | 2026-10-20，原软件版本发布日期 |
| License | MIT；仅适用于 ARCH 自有材料，保留第三方条款 |
| Access | Open |
| Language | English / eng |
| Keywords | hydrodynamics; adaptive mesh refinement; CUDA; reactive flows; nuclear reaction networks; scientific computing; ARCH Studio |
| Related work / identifier | [GitHub v1.3.0 Release](https://github.com/Shiro-Akane/ARCH/releases/tag/v1.3.0)，关系可用 Is supplement to，类型 Software |
| Repository | [Shiro-Akane/ARCH](https://github.com/Shiro-Akane/ARCH) |
| Source commit | `3063d883c1d7abf0b6bce5bb31df05af0a49cbac` |
| DOI | 若该版本已有记录则沿用；否则由所选草稿预留或发布时生成，当前引用文件省略 DOI，取得真实值后可回填 |
| Funding / Communities | 有真实适用信息才填；当前不虚构 |

## Creators：按顺序添加

| 顺序 | Family name | Given names | ORCID | Affiliation |
| --- | --- | --- | --- | --- |
| 1 | Ye | Haonan | 0009-0002-0706-1183 | 留空 |
| 2 | Jiang | Enjie | 0009-0000-8768-9557 | 留空 |
| 3 | Ding | Yu | 0009-0003-3677-6441 | 留空 |

## Description：可直接使用的英文正文

ARCH (Adaptive Reactive CUDA Hydrodynamics) is C++20/CUDA software for finite-volume simulation of compressible fluids, heat transfer and nuclear reactions with adaptive mesh refinement. CPU and CUDA backends share the physical and numerical mathematics. Version 1.3.0 adds the optional ARCH Studio desktop workflow for parameter editing, Configure/Build, real initial-field and initial-AMR previews, Run/Restart, and read-only Plotfile inspection.

This source deposit is based on the immutable v1.3.0 release commit 3063d883c1d7abf0b6bce5bb31df05af0a49cbac. The prepared Zenodo archive adds only post-release citation, AI-use disclosure and deposit documentation/metadata; ARCHIVE_PROVENANCE.json enumerates those additions and replacements. Scientific source, tests, original acceptance evidence and configuration remain at the tagged version. Git LFS pointers are retained as pointers; required external data and system dependencies must be obtained as described in the build guide. No raw simulation outputs, prebuilt scientific binaries or fetched LFS payloads are added.

Supported execution is Linux/WSL2; CUDA qualification for this release uses an RTX 3060 Ti (sm86). The published acceptance record maps 16 public models to their applicable evidence, and records 388 Studio tests without skips and the actual desktop workflow. Qualification remains scoped: SNIa uses the accepted joint evidence with the original repeated-AMR FAIL and thermal GAP retained; hotspot functional endpoints do not establish a separate ignition-mechanism result. GNN functionality is outside this release scope. The numerical tolerances and strict floating-point rules remain those of the published acceptance record.

ARCH-authored material is licensed under MIT. Third-party components, scientific sources and data retain their own terms in LICENSE, THIRD_PARTY_NOTICES.md and LICENSES/.

AI-assisted tools were used for bug tracking and fixes, review of tolerance settings across the complete CTest suite, editing selected documentation, code completion, and engineering and security checks. The overall software architecture was designed and built by the human contributors. Human maintainers retain responsibility for scientific definitions, numerical acceptance criteria, review, and release decisions.

## 引用与验收依据

- [v1.3.0 发布说明](../releases/V1.3.0.md)与[中文说明](../releases/V1.3.0.zh-CN.md)。
- [REL-6 适用范围验收](../../validation/backend/results/release-readiness/REL6CandidateAcceptance-20261020.json)：使用已记录证据，本轮未重跑科学验证或全 CTest。
- [构建指南](../guides/Build.md)、[Studio 指南](../guides/Studio.md)、[法律索引](../legal/README.md)。
- [AI 使用说明](AI_USAGE.md)；架构由人类搭建，AI 辅助范围已按维护者确认填写。
