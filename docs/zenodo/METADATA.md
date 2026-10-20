# ARCH 1.3.0：Zenodo 已发表元数据

核对日期：2026-10-21。以下内容与[已发表的 Zenodo 记录](https://zenodo.org/records/23503925)核对一致；记录中的软件发布日期为 2026-10-20。

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
| 本版本 DOI | [10.5281/zenodo.23503925](https://doi.org/10.5281/zenodo.23503925)，用于引用 ARCH 1.3.0 |
| 全版本 DOI | [10.5281/zenodo.23503924](https://doi.org/10.5281/zenodo.23503924)，用于指代版本系列 |
| 记录 URL | [Zenodo 23503925](https://zenodo.org/records/23503925) |
| Funding / Communities | 有真实适用信息才填；当前不虚构 |

## Creators：按顺序添加

| 顺序 | Family name | Given names | ORCID | Affiliation |
| --- | --- | --- | --- | --- |
| 1 | Ye | Haonan | 0009-0002-0706-1183 | Division of Physics and Astronomy, Graduate School of Science, Kyoto University |
| 2 | Jiang | Enjie | 0009-0000-8768-9557 | Graduate School of Advanced Science and Technology, Japan Advanced Institute of Science and Technology (JAIST) |
| 3 | Ding | Yu | 0009-0003-3677-6441 | 留空 |

## Description：已发表的英文正文

ARCH (Adaptive Reactive CUDA Hydrodynamics) is a C++20 and CUDA simulation code designed for finite-volume modeling of compressible hydrodynamics, heat transfer, and nuclear reactions with adaptive mesh refinement. Its CPU and CUDA backends share identical physical and numerical formulations. Version 1.3.0 introduces the optional ARCH Studio graphical desktop environment, providing researchers with an integrated workflow to edit simulation parameters, configure and build targets, inspect initial fields and AMR mesh layouts, execute or restart runs, and examine read-only plotfiles.

This deposit provides the complete source tree based on the tagged v1.3.0 release, accompanied by post-release citation information and deposit documentation outlined in ARCHIVE_PROVENANCE.json. The scientific source code and test configurations remain identical to the tagged release. No raw simulation outputs, prebuilt scientific binaries, or fetched Git LFS payloads are added. Git LFS pointers remain as pointers; users should obtain required external data and system dependencies as described in the build guide.

Supported execution is Linux and WSL2. Software qualification and validation remain scoped rather than universal; details regarding supported configurations, numerical tolerances, and specific validation limits are provided in the release notes. Researchers should not assume that all physical models, equation-of-state or reaction network combinations, execution devices, or scientific mechanisms have been validated. In addition, GNN functionality is outside the scope of this release.

All ARCH-authored material is licensed under MIT. Third-party components, scientific sources, and data retain their own terms and notices as detailed in LICENSE, THIRD_PARTY_NOTICES.md, and the LICENSES/ directory.

AI-assisted tools were used for bug tracking and fixes, review of tolerance settings across the complete CTest suite, editing selected documentation, code completion, and engineering and security checks. The overall software architecture was designed and built by human contributors. Human maintainers retain responsibility for scientific definitions, numerical acceptance criteria, code review, and release decisions.

## 引用与验收依据

- [v1.3.0 发布说明](../releases/V1.3.0.md)与[中文说明](../releases/V1.3.0.zh-CN.md)。
- [REL-6 适用范围验收](../../validation/backend/results/release-readiness/REL6CandidateAcceptance-20261020.json)：使用已记录证据，本轮未重跑科学验证或全 CTest。
- [构建指南](../guides/Build.md)、[Studio 指南](../guides/Studio.md)、[法律索引](../legal/README.md)。
- [AI 使用说明](AI_USAGE.md)；架构由人类搭建，AI 辅助范围已按维护者确认填写。
