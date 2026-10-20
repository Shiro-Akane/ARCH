# Zenodo 填写底稿

本底稿含待确认项，不能整页直接粘贴发布。作者职责来自维护者说明；发布版本为 GUI 彻底收尾完成后的版本，GNN 不在本次范围内。已读取当前分支的 `docs/legal/README.md` 和根目录 `LICENSE`，法律原文按该索引保留于根目录及 `LICENSES/`；软件功能介绍与许可字段须依据最终发布提交及既有材料补齐。

| Zenodo 字段 | 当前底稿 / 操作 |
| --- | --- |
| Resource type | Software |
| Title | 候选：`ARCH: scientific computing software`；正式名称与副标题由维护者确认 |
| Version | GUI 收尾版本的正式软件版本号，待冻结；不要自动使用分支名 `compute/optim` |
| Publication date | 实际发布日期；准备日期不是发布日期 |
| Creators | 按最终顺序填写：Haonan Ye、Enjie Jiang、Yu Ding，详见下表 |
| Description | 使用下方英文框架，先补齐实际功能、验证范围与限制 |
| Keywords | 候选：`scientific computing`, `CUDA`, `GPU computing`, `graphical user interface`；仅保留与归档范围一致的词，不加入 GNN |
| License | 依据 `docs/legal/README.md` 和根目录 `LICENSE` 核对最终归档范围后填写；ARCH 自有材料为 MIT，第三方许可另见 `THIRD_PARTY_NOTICES.md` 和 `LICENSES/`，不另拟许可 |
| DOI | 先查是否已有记录/DOI；未有则按所选入口生成或预留 |
| Related identifiers | 实际源码仓库 URL、本次版本 Release/标签 URL；如有相关论文再核实添加 |
| Software-specific fields | 按表单与实际代码填写源码 URL、语言、开发状态等；不要推测依赖或支持平台 |
| Funding | 有真实资助信息才填写，无则不虚构 |
| Communities | 可选；核实社区确实存在且相关后选择 |

## 作者身份表

| 最终顺序 | Family name | Given names | ORCID | Affiliation |
| --- | --- | --- | --- | --- |
| 1 | Ye | Haonan | 0009-0002-0706-1183 | 留空（个人项目，按维护者要求） |
| 2 | Jiang | Enjie | 0009-0000-8768-9557 | 留空（个人项目，按维护者要求） |
| 3 | Ding | Yu | 0009-0003-3677-6441 | 留空（个人项目，按维护者要求） |

姓名、ORCID 对应关系及最终署名顺序已由维护者确认；机构按要求暂不填写，不使用 `Independent researcher` 等未经确认的替代文字。三位开发者均列入 Creators，按上表顺序排列。用 ORCID 搜索/补全时仍核对英文姓名与姓/名拆分，避免自动补全覆盖这里的确认信息。Creators 会出现在学术引用中，Contributors 用于不列入引用的人或机构。[官方字段说明](https://help.zenodo.org/docs/deposit/describe-records/creators/)

## 英文描述框架（必须完成 TODO 后才能使用）

> ARCH is scientific computing software. This record archives the source code of version TODO_VERSION, corresponding to commit TODO_COMMIT_SHA in TODO_REPOSITORY_URL, after completion of GUI development and validation for this release.
>
> The scope of this release is TODO_VERIFIED_RELEASE_SCOPE. Build instructions, dependencies, usage examples, and known limitations are provided in TODO_ACTUAL_DOCUMENTATION_PATHS. Validation performed for this release consists of TODO_ACTUALLY_COMPLETED_CHECKS, with TODO_UNVERIFIED_SCOPE remaining unverified.
>
> Haonan Ye is responsible for architecture discussions and the remaining project work. Enjie Jiang is primarily responsible for CUDA development and optimization, and Yu Ding is primarily responsible for GUI development, refinement, and optimization.

这是用于 GUI 收尾完成后的待完成描述框架，不是当前 GUI 已完成或功能已获验证的陈述。GNN 不在本次发布范围内，不加入本版本的功能或贡献描述。具体贡献说明见 `CONTRIBUTIONS.md`；姓名和最终作者顺序已由维护者确认，发布前核对表单与引用文件保持一致。

## 本次版本说明待填

- 本次归档目的及适用范围：GUI 彻底收尾完成后的软件版本；具体能力按最终验收结果补齐。
- 包含的主要组件：待按实际源码与构建入口核实。
- 实际完成的测试/最小运行及环境：待填。
- CUDA 支持范围与限制：待核实。
- GUI 集成范围、收尾完成情况与约定验收结果：待最终版本核实，不以准备材料代替验收。
- GNN：明确排除在本次发布与验收范围之外；归档及构建依赖需按此边界核对。
- 法律文件：依据 `docs/legal/README.md` 引用既有原文；已读取根目录 `LICENSE`，最终归档的适用范围与许可标识仍需核对。
- 复现所需的额外数据、子模块或 LFS 内容：待核实。
- 已知问题和不支持情形：待填。
