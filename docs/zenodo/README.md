# ARCH citation and Zenodo status

[发布说明](../releases/README.md) · [已发表元数据](METADATA.md) · [贡献说明](CONTRIBUTIONS.md) · [AI 使用说明](AI_USAGE.md)

## v1.4.0：GitHub 软件版，Zenodo 暂缓

软件发布日期为 **2026-10-27**，正式入口为 [GitHub v1.4.0](https://github.com/Shiro-Akane/ARCH/releases/tag/v1.4.0)。主要新增原生 Windows CPU／CUDA CLI 与 Studio 对应功能；Linux 仍推荐，Windows 保留受测范围及稳定性限制。

- 根目录 `CITATION.cff`、`.zenodo.json` 及本目录副本同步实际软件日期、版本、作者、机构、许可与 GitHub release URL。
- CFF DOI 仍为既有全版本系列 [10.5281/zenodo.23503924](https://doi.org/10.5281/zenodo.23503924)，**不是 v1.4.0 专属 DOI**。Zenodo 暂缓期间，引用这一版时同时给出版本、日期和 GitHub release URL；不要用1.3.0版本 DOI 代替。
- `.zenodo.json` 的 `publication_date` 是软件版本发布日期，供后续集成准备；它不表示 Zenodo 新版本已经发表。本轮没有上传、创建或发布 deposit，也不替换旧包。
- [发布记录](../histroy/release/V1.4.0Publication-20261027.zh-CN.md)区分已合并实现、元数据提交与标签／附件身份。[此前候选记录](../histroy/release/V1.4.0ReleasePreparation-20261027.zh-CN.md)保持当时的“未发布”状态，作为历史，不是当前引用入口。

## 已发表与历史记录

1.3.0 已发表的版本 DOI 为 [10.5281/zenodo.23503925](https://doi.org/10.5281/zenodo.23503925)，入口为 [23503925](https://zenodo.org/records/23503925)。该 DOI 与原包只标识1.3.0，不用于1.3.1或1.4.0。具体已发表字段仍在 [METADATA.md](METADATA.md)。

GitHub [v1.3.1](https://github.com/Shiro-Akane/ARCH/releases/tag/v1.3.1) 已发布；其 Zenodo 新包在先前记录中仍待处理。此前1.3.1／1.3.0状态、摘要、DOI与日期见[历史快照](../histroy/release/ZenodoCitationStatusBeforeV1.4.0-20261027.zh-CN.md)，不改写旧版本事实。

## 文件与分发

| 文件 | 用途 |
| --- | --- |
| [CITATION.cff.example](CITATION.cff.example) | 与[根目录引用文件](../../CITATION.cff)逐字一致的软件版本副本 |
| [.zenodo.json.example](.zenodo.json.example) | 与[根目录集成元数据](../../.zenodo.json)逐字一致；不代表 Zenodo 已发布 |
| [METADATA.md](METADATA.md) | 已发表1.3.0记录，保留原始身份 |
| [CONTRIBUTIONS.md](CONTRIBUTIONS.md)／[AI_USAGE.md](AI_USAGE.md) | 作者机构、职责与AI披露 |

GitHub release 的源码 ZIP 从实际验收通过的 main 标签生成，附 SHA-256 和 `ARCHIVE_PROVENANCE.json`；不会把旧候选 ZIP 改名为正式包。保留 LFS 指针与第三方许可，不新增本机指令、skill、旧根计划、下载载荷、原始 H5／checkpoint 或预编译科学程序。桌面便携包和安装器不包含在此源码发布中。

## Zenodo 恢复后的步骤

通过既有系列的 **New version** 准备同一软件版本存档，核对作者、版本、文件和摘要后再发布；取得实际版本 DOI 后据实回填，区分版本 DOI 与全版本 DOI。旧标签和已发表包不移动、不覆盖。见[官方版本说明](https://help.zenodo.org/docs/deposit/manage-versions/)。
