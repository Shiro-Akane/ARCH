# ARCH citation and Zenodo status

[发布说明](../releases/README.md) · [已发表元数据](METADATA.md) · [贡献说明](CONTRIBUTIONS.md) · [AI 使用说明](AI_USAGE.md)

## v1.4.0：候选元数据，尚未发表

本轮为 [PR #21](https://github.com/Shiro-Akane/ARCH/pull/21) 准备 v1.4.0，主要新增原生 Windows CPU／CUDA CLI 与 Studio 对应功能。标签、公开 release 和 Zenodo 新记录尚未创建；由用户合并后再进入发布出口。

- 根目录 `CITATION.cff`、`.zenodo.json` 及本目录副本同步候选版本、作者、机构、许可和说明，URL 指向已存在的 PR，而不是尚不存在的 v1.4.0 release。
- CFF 中 DOI 仍为既有全版本系列 [10.5281/zenodo.23503924](https://doi.org/10.5281/zenodo.23503924)，不冒充 v1.4.0 专属 DOI。
- 候选不填写尚未发生的 `date-released`／`publication_date`；公开前补实际日期与真实 release URL，并验证元数据及对应提交。
- Zenodo 继续暂缓；没有上传、创建或发布新 deposit，也没有替换旧包。

## 已发表与历史记录

1.3.0 已发表的版本 DOI 为 [10.5281/zenodo.23503925](https://doi.org/10.5281/zenodo.23503925)，记录入口为 [23503925](https://zenodo.org/records/23503925)。该 DOI 与原包只标识1.3.0，不用于1.3.1或1.4.0。具体已发表字段仍在 [METADATA.md](METADATA.md)。

GitHub [v1.3.1](https://github.com/Shiro-Akane/ARCH/releases/tag/v1.3.1) 已发布；其 Zenodo 新包在先前记录中仍待处理。原1.3.1／1.3.0状态、包摘要、DOI与日期见[准备1.4.0前的归档快照](../histroy/release/ZenodoCitationStatusBeforeV1.4.0-20261027.zh-CN.md)，不改写旧版本事实。

## 文件与分发

| 文件 | 用途 |
| --- | --- |
| [CITATION.cff.example](CITATION.cff.example) | 与[根目录引用文件](../../CITATION.cff)逐字一致的候选副本 |
| [.zenodo.json.example](.zenodo.json.example) | 与[根目录集成元数据](../../.zenodo.json)逐字一致的候选副本 |
| [METADATA.md](METADATA.md) | 已发表1.3.0记录，保持历史身份 |
| [CONTRIBUTIONS.md](CONTRIBUTIONS.md)／[AI_USAGE.md](AI_USAGE.md) | 作者机构、职责与AI披露 |

源码归档以最终被接受的提交／标签为准，保留 LFS 指针与第三方许可，排除本机指令、skill、旧根计划、下载载荷、原始 H5／checkpoint 和预编译科学程序。候选 ZIP 与正式发表的包分别标识；不能把旧 ZIP 改名为新版本。

## 合并后的发布步骤

1. 先验收实际 main 提交及其 CI／安全状态，更新真实 release URL 和实际日期；新增元数据提交同样须验收。
2. 用户授权正式发表后，才建立不可移动的 v1.4.0 标签与源码 release；不移动或覆盖旧标签／发布物。
3. Zenodo 恢复后，通过既有系列的 **New version** 创建对应存档；核对作者、文件、许可和摘要后再发布。取得实际版本 DOI 后据实回填，区分版本 DOI 与全版本 DOI。见[官方版本说明](https://help.zenodo.org/docs/deposit/manage-versions/)。
