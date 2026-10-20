# ARCH 1.3.0：Zenodo 提交指南

核对日期：2026-10-20。v1.3.0 已正式发布；本目录已补齐版本、作者、许可、描述与 AI 使用披露。本轮补充在 `main` 分支维护。需由维护者完成的账号操作为：确认已有记录、上传归档、确认或生成 DOI、核对表单并最终发布。

## 已确认的发布身份与范围

- 标题：ARCH: Adaptive Reactive CUDA Hydrodynamics；版本：1.3.0；原软件发布日期：2026-10-20。
- 已发布标签：`v1.3.0`；源码提交：`3063d883c1d7abf0b6bce5bb31df05af0a49cbac`；[GitHub Release](https://github.com/Shiro-Akane/ARCH/releases/tag/v1.3.0)。
- GUI 收尾及声明范围已获正式发布验收。GNN 不属于本次支持范围。
- 支持与限制沿用发布说明及 REL-6 验收记录；本轮补元数据不改写科学结果、数值门槛或历史失败。
- 三位作者按 Haonan Ye、Enjie Jiang、Yu Ding 排列，ORCID 已确认，机构留空。
- ARCH 自有材料为 MIT；法律原文保留于根目录 `LICENSE`、`THIRD_PARTY_NOTICES.md` 与 `LICENSES/`，见[法律索引](../legal/README.md)。

## 文件用途

| 文件 | 用途 |
| --- | --- |
| [填写底稿](METADATA.md) | 表单固定字段及可复制的完整英文描述 |
| [贡献说明](CONTRIBUTIONS.md) | 最终作者顺序、ORCID 与职责 |
| [AI 使用说明](AI_USAGE.md) | 中英文披露，明确人类负责整体架构 |
| [CITATION.cff.example](CITATION.cff.example) | 与[根目录正式引用文件](../../CITATION.cff)一致的副本 |
| [.zenodo.json.example](.zenodo.json.example) | 与[根目录正式集成元数据](../../.zenodo.json)一致的副本 |

两份 `.example` 已完成固定字段，不再含 TODO 占位符；根目录文件才供相关集成读取。DOI 未取得时不填虚构值。若两种根目录元数据同时存在，Zenodo 的 GitHub 集成优先使用 `.zenodo.json`。[官方说明](https://help.zenodo.org/docs/github/describe-software/)

## 本次使用的归档

现有 v1.3.0 GitHub Release 已发布且不可变；新增引用和披露文件不能自动进入旧标签的源码包。本次准备手动上传的 `ARCH-1.3.0-zenodo-source.zip`：以该标签的源码为基底，仅补入根目录引用文件及本目录的发布元数据与说明文档与说明文档与说明文档与说明文档与说明文档与说明文档，包内 `ARCHIVE_PROVENANCE.json` 列明全部新增／替换路径和摘要。所有其他已标记文件保持原字节；它是“标签源码加发布后元数据”，不是标签树的逐字节副本。

归档及校验清单生成于本工作树 `output/release/zenodo-v1.3.0-20261020/`，并交付一份可直接从 Windows 选择上传的副本。校验摘要以相邻 `.sha256` 和 `submission-manifest.json` 为准。只上传最终 ZIP；这些校验旁文件供维护者核对。

Git LFS 指针原样保留。源码包不含额外下载的大型表、原始 H5／checkpoint 或预编译科学执行物；表和依赖的取得／离线复用沿构建指南。GNN 的历史文字若存在于标签中仍按原样保存，不将其解释为本版本支持能力。

## 需要维护者在 Zenodo 完成的步骤

1. 登录 [Zenodo](https://zenodo.org/)，先查看自己的上传记录，确认 ARCH 或 1.3.0 是否已有 DOI。已有 1.3.0 记录则编辑该记录；若此前已发表其他版本，从原记录点 **New version**；首次存档才创建 **New upload**，避免同版本重复 DOI。[版本管理](https://help.zenodo.org/docs/deposit/manage-versions/)
2. 将最终 `ARCH-1.3.0-zenodo-source.zip` 上传到选定草稿。若需要 Software Heritage 归档，按官方流程仅保留这一个源码压缩文件，说明及披露均已放在包内。[手动软件上传](https://help.zenodo.org/docs/github/archive-software/manual-upload/)
3. 按[填写底稿](METADATA.md)填写 Software、标题、1.3.0、原软件发布日期、三位 Creators／ORCID、MIT、关键词和完整英文 Description。机构留空。AI 披露已包含在英文 Description 中。
4. 若此软件版本已有 DOI，按真实已有值填写；否则选择没有已有 DOI，可点 **Get a DOI now!** 预留，也可由发布时生成。[DOI 操作](https://help.zenodo.org/docs/deposit/describe-records/reserve-doi/)
5. 若希望 ZIP 内引用文件也包含刚预留的 DOI，先把真实 DOI 回传，由维护者更新两种正式引用／元数据及副本后，重新生成并核对源码包，再上传替换草稿中的旧 ZIP。这个步骤只更新发布后元数据，不移动 v1.3.0 标签。也可直接使用当前无 DOI 的源码包，发表后在仓库补引用入口。
6. 保存并预览，确认文件名、作者顺序、姓名／ORCID、版本、许可和 Description 后，由维护者点击 **Publish**。保存草稿或预留 DOI 不等于发表。
7. 返回版本 DOI、Zenodo 记录 URL，以及记录提供的全版本 DOI（若有），再补齐仓库引用入口。论文和具体结果优先引用实际使用版本的 DOI。

不要为本次手动记录另外重发一次 GitHub v1.3.0 Release。GitHub 自动集成可在后续版本发布前启用，新版本将读取该标签包含的正式元数据。[启用仓库](https://help.zenodo.org/docs/github/enable-repository/)

## 发布后记录

| 项目 | 当前值 |
| --- | --- |
| 软件版本／标签 | 1.3.0 / v1.3.0 |
| 原始源码提交 | `3063d883c1d7abf0b6bce5bb31df05af0a49cbac` |
| Zenodo 发布入口 | 本次手动上传；已有版本系列优先沿用 |
| 源码归档校验 | 相邻 `.sha256` 与 `submission-manifest.json` |
| 本版本 DOI / 记录 URL | 由维护者完成账号操作后记录 |
| 全版本 DOI | 若记录提供则另记 |
| 固定声明 | 人类搭建整体架构；AI 辅助范围见披露；GNN 不在范围内 |
