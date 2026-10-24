# ARCH 1.3.1：归档与引用状态

本维护版的源码入口为 [GitHub v1.3.1](https://github.com/Shiro-Akane/ARCH/releases/tag/v1.3.1)，变更与验证范围见[发布说明](../releases/V1.3.1.zh-CN.md)。

- 版本号：1.3.1；软件发布日期：2026-10-24。
- 继续使用既有 Zenodo 版本系列：全版本 DOI [10.5281/zenodo.23503924](https://doi.org/10.5281/zenodo.23503924)。
- 1.3.1 的 Zenodo 新版本包尚待上传并发布；不把 1.3.0 的记录或 DOI 写成 1.3.1 已发表。
- 根目录 `CITATION.cff` 与副本使用 release URL 标识 1.3.1，并明确使用全版本 DOI；`.zenodo.json` 与副本同步版本、日期、作者、机构、许可与描述，暂不填写新版本 DOI。
- 新包以冻结标签为源码依据，排除本机 assistant 指令、skill 和两份完成计划。保留 LFS 指针、许可与已有科学证据；不加入原始模拟输出或预编译科学执行物。

在既有记录的 **New version** 中上传新包，核对作者、说明、文件及校验值后发布；取得新版本 DOI 后再据实回填，不修改已发表 1.3.0 包。官方[版本管理说明](https://help.zenodo.org/docs/deposit/manage-versions/)解释版本系列的延续方式。

## 1.3.0 历史记录

以下保留原已发表版本的存档说明；其中“根目录当前值”等说法对应 2026-10-21 的回填时点。原版本 DOI、包与校验值保持历史身份。

---

# ARCH 1.3.0：Zenodo 发布记录与引用

核对日期：2026-10-21。ARCH 1.3.0 已在 Zenodo 正式发表；本目录及根目录引用文件同步已发表记录的 DOI、作者机构和英文说明。

## 正式引用入口

| 项目 | 当前值 |
| --- | --- |
| 标题 | ARCH: Adaptive Reactive CUDA Hydrodynamics |
| 软件版本／标签 | 1.3.0 / v1.3.0 |
| 软件发布日期 | 2026-10-20 |
| 本版本 DOI | [10.5281/zenodo.23503925](https://doi.org/10.5281/zenodo.23503925) |
| Zenodo 记录 | [23503925](https://zenodo.org/records/23503925) |
| 全版本 DOI | [10.5281/zenodo.23503924](https://doi.org/10.5281/zenodo.23503924) |
| GitHub Release | [v1.3.0](https://github.com/Shiro-Akane/ARCH/releases/tag/v1.3.0) |

论文和具体计算结果应引用实际使用版本的 DOI；全版本 DOI 用于指代 ARCH 版本系列。根目录 `CITATION.cff` 和 `.zenodo.json` 中的 DOI 均指向 1.3.0。

Ye, H., Jiang, E., & Ding, Y. (2026). *ARCH: Adaptive Reactive CUDA Hydrodynamics* (Version 1.3.0) [Computer software]. Zenodo. https://doi.org/10.5281/zenodo.23503925

作者顺序为 Haonan Ye、Enjie Jiang、Yu Ding；前两位机构按已发表记录填写，Yu Ding 的机构留空，详见[贡献说明](CONTRIBUTIONS.md)。GUI 属于本次发布范围，GNN 不在范围内。支持范围及限制沿用发布说明与原验收记录。

## 文件用途

| 文件 | 用途 |
| --- | --- |
| [已发表元数据](METADATA.md) | 正式 DOI、记录链接、字段与完整英文说明 |
| [贡献说明](CONTRIBUTIONS.md) | 最终作者顺序、ORCID、机构与职责 |
| [AI 使用说明](AI_USAGE.md) | 中英文披露；整体架构由人类设计并搭建 |
| [CITATION.cff.example](CITATION.cff.example) | 与[根目录引用文件](../../CITATION.cff)一致的副本 |
| [.zenodo.json.example](.zenodo.json.example) | 与[根目录集成元数据](../../.zenodo.json)一致的副本 |

根目录文件供相关集成读取；副本随之同步。ARCH 自有材料采用 MIT，第三方材料保留其原条款；法律原文与入口见[法律索引](../legal/README.md)。

## 已发表的源码归档

记录中的文件为 `ARCH-1.3.0-zenodo-source.zip`，大小为 30,342,501 字节。它以已发布的 v1.3.0 标签源码为基底，仅补入发布后引用、披露与存档说明；包内 `ARCHIVE_PROVENANCE.json` 列明新增／替换路径。其他已标记文件保持原字节。

原始源码提交为 `3063d883c1d7abf0b6bce5bb31df05af0a49cbac`。归档的 SHA-256 为 `1cd4ef1f781663bba747f36a3301458518498d06abd483971a930551ff4fab77`；Zenodo 显示的 MD5 为 `63e31dfd75f9c03eba6f1826a1a6afa9`。本地归档及校验旁文件保留在 `output/release/zenodo-v1.3.0-20261020/`。

该 ZIP 在正式 DOI、最终机构及润色后的网页说明回填仓库前已生成。仓库现行引用文件反映已发表的网页元数据；此次回填不替换已发表 ZIP、不移动标签。科学源码、测试配置及原验收证据保持原身份。

Git LFS 指针原样保留。包中未额外加入下载的大型表、原始 H5／checkpoint 或预编译科学执行物；依赖取得与离线复用见[构建指南](../guides/Build.md)。

## 后续版本维护

1. 从既有 Zenodo 记录使用 **New version** 延续同一版本系列，避免为同一版本重复创建记录。
2. 准备新版本时同步更新版本号、日期、作者信息及实际归档；取得新版本 DOI 后更新根目录引用文件与本目录副本。不要把 1.3.0 的 DOI 当作新版本 DOI。
3. 核对文件、说明、作者和许可后由维护者发布，再确认版本 DOI 与全版本 DOI 的用途。版本管理见 [Zenodo 官方指南](https://help.zenodo.org/docs/deposit/manage-versions/)。

本次为手动上传，无需重新发布 GitHub v1.3.0 Release。未来如启用 GitHub 集成，按[官方元数据说明](https://help.zenodo.org/docs/github/describe-software/)维护对应发布标签中的引用文件。
