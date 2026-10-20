# ARCH 的 Zenodo 发布准备

准备日期：2026-10-05。迁移日期：2026-10-06。目标分支：`compute/optim`。

状态：发布前草稿。提交版本确定为 GUI 彻底收尾完成后的版本；GNN 不在本次发布范围内。五份准备材料位于 `docs/zenodo/`，在 `compute/optim` 分支维护。已读取当前分支的法律索引及根目录许可证；最终软件功能、归档范围和 GUI 验收结果仍须在发布前核对。本次迁移不是软件发布，未创建 Zenodo 记录、GitHub Release、版本标签或 DOI。

## 本目录文件

| 文件 | 用途 |
| --- | --- |
| `CONTRIBUTIONS.md` | 三位参与者的 ORCID、职责及英文贡献说明候选 |
| `METADATA.md` | Zenodo 表单逐字段填写底稿和待确认项 |
| `CITATION.cff.example` | GitHub 软件引用文件候选；确认信息后放到发布仓库根目录 |
| `.zenodo.json.example` | 可选的 GitHub–Zenodo 集成元数据候选；确认后才放到根目录 |
| 本文件 | 文件准备、分支核对、归档与发布流程 |

`.example` 文件含 `TODO_` 占位符，不可直接用于发布。放在 `docs/zenodo` 的模板不会自动成为仓库根目录的引用/集成元数据。若根目录已有同名正式文件，应核对并更新已有文件，避免覆盖现有作者或元数据。

## 已确认的作者信息

维护者已确认最终作者顺序为 Haonan Ye（0009-0002-0706-1183）、Enjie Jiang（0009-0000-8768-9557）和 Yu Ding（0009-0003-3677-6441）。本项目为个人项目，暂不填写机构。Zenodo 表单、底稿与引用模板均按上述最终顺序排列。

## 已确定的提交范围

- 发布基线：GUI 彻底收尾并完成约定验收后的版本，不以当前准备文档的提交作为正式软件版本。
- GNN 不属于本次发布、功能说明或验收范围；不将其列为本版本能力、关键词或复现依赖。
- 发布前记录 GUI 约定的关键工作流及实际验证结果，确认收尾完成后再冻结版本号、标签和完整提交 SHA。
- 源码包按此范围核对，排除 GNN 专属实验材料、模型权重与数据；若代码仍存在耦合，先处理发布范围与构建依赖，不能仅删掉描述就宣布范围已满足。
- 法律材料以 [法律索引](../legal/README.md) 及其指向的根目录 `LICENSE`、`THIRD_PARTY_NOTICES.md` 和 `LICENSES/` 中的既有文件为准，不复制或迁移法律原文。发布前核对适用范围以及 Zenodo/CFF 的许可标识，并确保所需原文随软件归档提供。

迁移时已读取 `docs/legal/README.md` 和根目录 `LICENSE`：ARCH 自有材料采用 MIT，第三方组件另有许可与声明。此处仅记录已读取的信息，最终发布仍须依据归档内容核对第三方条款。

## 先确认这些信息

- ARCH 的正式软件名称、仓库 URL、软件许可证和第三方文件许可。
- 本次发布版本、标签、发布日期和完整提交 SHA；`compute/optim` 是分支名，不自动等于软件版本号。
- 是否已有 ARCH 的 Zenodo 记录及 DOI；有则优先沿用原记录的版本系列。
- 本次版本实际包含且已验证的功能、未完成部分、构建与最小运行说明。
- GUI 收尾版本的集成与验收结果；确认归档范围及构建依赖符合本次不包含 GNN 的要求。

## 流程 1：核对并整理 `compute/optim`

1. 在实际可访问的 ARCH 工作树运行 `git status --short --branch` 与 `git branch --show-current`，确认分支。不要在存在未知改动时强制切换或重置。
2. 本工作树已挂靠到 `compute/optim`，无需重复切换。若后续在其他工作树继续，用 `git worktree list` 确认该分支所在目录，不强制重复检出或覆盖同名未跟踪文件。
3. 将本目录材料核对后纳入 `compute/optim`；检查目标仓库的 `AGENTS.md`、现有 `docs` 索引、README、许可证和引用文件。
4. 完成 GUI 收尾与约定验收后，依据最终代码、构建入口及已运行验证完成 `METADATA.md` 的软件介绍和本次版本说明；GNN 不纳入本次。不要把 README 中的设计或源码存在当作功能已经验证的证据。
5. 沿用已确认的作者顺序并补齐版本信息，再生成/更新仓库根目录的正式 `CITATION.cff`。如需要 Zenodo 专有字段，再采用 `.zenodo.json`。

若两个根目录文件同时存在，Zenodo 的 GitHub 归档使用 `.zenodo.json`，因此两者的作者、标题、版本、许可证和 DOI 必须一致。只需要基本软件引用时可以只用 `CITATION.cff`。[官方说明](https://help.zenodo.org/docs/github/describe-software/)

## 流程 2：冻结可复现的归档内容

归档包至少应包括本次发布范围内的源代码、法律索引指向的适用法律原文、构建/依赖说明、最小使用示例、引用信息、贡献说明，以及本次版本的已知限制。法律路径见 `docs/legal/README.md`，其他材料仍需按最终发布版本核对。

- 不从随时变化的分支直接描述一个永久版本；记录最终提交 SHA，再让版本标签指向该提交。
- 检查子模块与 Git LFS。`git archive` 不能保证把它们的实际内容一并归档，需要另行验证或提供可复现取得方法。
- 不把个人路径、密钥、本地构建缓存或无关大型模拟输出加入源码归档。
- 对实际归档包检查目录清单、许可证、版本和关键输入文件；只需对最终发布压缩包记录校验值，无需全仓库逐文件哈希。
- 从解压出的归档包按文档完成适合本次发布范围的构建和最小运行；记录平台、依赖版本、命令、结果及未验证范围。
- 确认 GUI 的验收结果对应归档包中的实际版本，且该包的使用与复现不依赖范围外的 GNN 材料。

## 流程 3：选定一个发布入口

首次准备时建议先选定一种入口并沿用，避免为同一个软件版本建立两个独立 DOI 记录。

### A. 手动上传：适合先填写并复核 Zenodo 草稿

1. 登录 Zenodo，检查是否已有本软件记录。首次发布创建 New upload；已有版本系列则从原记录创建 New version。[版本管理](https://help.zenodo.org/docs/deposit/manage-versions/)
2. Resource type 选 `Software`。按 `METADATA.md` 填标题、作者/ORCID、描述、版本、日期、许可及相关链接。
3. 若没有现有 DOI，在 DOI 栏选择没有已有 DOI，并点击 `Get a DOI now!` 预留。预留后把真实版本 DOI 写入本次归档所需引用文件；保留该草稿。[DOI 官方说明](https://help.zenodo.org/docs/deposit/describe-records/reserve-doi/)
4. 完成最终提交及归档包，上传实际源码压缩包。若希望被 Software Heritage 自动归档，按官方要求该记录仅上传一个包含源码的压缩文件；需要的说明文件放在包内。[手动软件上传说明](https://help.zenodo.org/docs/github/archive-software/manual-upload/)
5. 保存并预览草稿，逐项核对作者顺序、姓名/ORCID 对应关系、许可证、版本、文件内容及链接。手动上传时直接填写表单；不要假定包内 `.zenodo.json` 会替你填表。
6. 全部核对完成后，由维护者点击 Publish。预留 DOI 不代表已发布。
7. 访问记录和 DOI，下载核对文件；将记录 URL、版本 DOI、版本标签与 SHA 记入发布记录，再更新 README 引用入口。

### B. GitHub 自动归档：适合以 GitHub Release 管理软件版本

1. 登录 Zenodo 并关联 GitHub；在 Zenodo 的 GitHub 页面同步仓库并启用 ARCH。[启用仓库](https://help.zenodo.org/docs/github/enable-repository/)
2. 在 `compute/optim` 的最终发布提交中确认根目录正式引用/元数据文件已经补齐并校验。
3. 核对目标提交、版本标签和 Release 说明，随后发布 GitHub Release。启用集成后新 Release 会触发自动归档；普通推送分支不等于归档发布。
4. 等待 Zenodo 处理，核对产生的记录及 DOI。失败时查看该 Release 对应的 Errors，再按问题修正；不要通过新建独立手动记录盲目补救。[GitHub Release 归档](https://help.zenodo.org/docs/github/archive-software/github-upload/)
5. 首次自动归档前若未知 DOI，不填虚构值。成功后将真实 DOI 加入后续维护提交；已归档的那个版本仍对应原发布提交，不移动已发布标签来补写 DOI。

手动预留 DOI 的草稿与 GitHub 自动归档是不同入口；不能假定后者会复用前者的草稿或预留 DOI。

## 发布记录待填写

| 项目 | 值 |
| --- | --- |
| 发布入口 | 待选：手动上传 / GitHub 集成 |
| 最终分支 | `compute/optim`；本目录随分支版本管理 |
| 发布里程碑 | GUI 彻底收尾并完成约定验收 |
| 发布范围 | GUI 收尾后的软件版本；GNN 不在范围内 |
| 既有法律文件路径 / 许可标识 | 索引：`docs/legal/README.md`；原文：根目录 `LICENSE`、`THIRD_PARTY_NOTICES.md` 和 `LICENSES/`；最终归档许可字段待核对 |
| 完整提交 SHA | 待填 |
| 版本 / 标签 | 待填 |
| 归档文件名 / 校验值 | 待填 |
| 验证环境与结果 | 待填 |
| Zenodo 记录 URL | 待填 |
| 本版本 DOI | 待填 |
| 全版本 DOI（若记录提供） | 待填；与本版本 DOI 分别记录 |
| 发布日期 | 待填实际日期 |

具体实验与结果引用本版本 DOI，以便定位实际文件；项目总入口可使用记录提供的全版本 DOI。后续软件更新应保持原版本系列，不替换已发布版本的标签含义。
