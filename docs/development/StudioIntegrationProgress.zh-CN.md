# 联合交付执行清单

本轮依据 StudioConfigurationHandoff.zh-CN.md，完整范围保留：3B 收尾、O7.0 配置 v3、
Linux/WSL 3C、模型初态/AMR、O7.1–O7.5、CPU 后 CUDA、冻结方案短测和批准的长时子集。
plt 按独立出口交付。Windows 适配/安装包、O8/O10、main 合并均不在本轮实施范围。

## 取得的准确基线

- compute/optim：8fc0dd25eefd2243e8c36f85440bac46994e2e73。
- Studio：c96e9da0a6d114fd0323102b73dba4a3cd8da9ba / studio-phase3b-v0.21.0。
- 独立分支：studio/compute-optim-integration。
- 仅按 Git tree 引入封箱 studio/ 子树，导入前后子树对象一致。
- 非 Studio 来源保留 compute/optim；相对 main 的上游增量为文档/既有审计，没有覆盖 Core。
- 原 3B 工作区 clean 且未修改；原 tag 不移动。
- 旧 3B 测试结果只是历史证据，本轮复验独立记录。
- npm test 包含 Host，不能把总数与 Host 子集相加。

## 顺序清单

| 阶段 | 实现 | 本轮工程验证 | 科学 review / 性能 |
| --- | --- | --- | --- |
| 1 3B 源码接收与复验 | 封箱源码已引入；Linux 打开/另存/重开复验通过 | npm ci、177 tests、lint/typecheck/build PASS；Linux 原生打开、Host Save As、重开 PASS（范围见报告） | 不适用 |
| 2 O7.0 + 配置 v3/Host/Studio | 候选规范与共享 schema/Sod/缺项/语法 fixture 已整理；运行实现待迁移 | 候选一致性检查通过；生产 v3 未验证 | 科学条件按唯一计划；疑点交维护者 |
| 3 Linux/WSL 3C 启动/Configure/Build | 待实施 | 待执行 | 不适用 |
| 4 3C Run/Restart/进程隔离 | 待实施，依赖新配置契约 | 待执行 | 小型有效输入 |
| 5 全模型初态/AMR | 待实施 | 待逐模型验收 | 真实域/预算需明确 |
| 6 O7.1 JENS | 待实施 | 先 CPU | 独立参考/预算由维护者确认 |
| 7 O7.2–O7.5 RZ | 待实施 | 分层 CPU | O7.4 科学方案须 review |
| 8 CUDA/第二平台短测 | 待 CPU 完成 | 未编译/未计时 | 冻结同物理终点；保留负收益 |
| 9 批准的 O9 长时子集 | 待冻结输入/预算 | 未执行 | 未批准项不能称完成 |
| 独立 plt 只读出口 | 待 3C 后/文件语义确认 | 未执行 | 原生单元与数据身份 |

## 不变量与交付

不更改独立参考、物理定义或误差阈值以求通过。不以 CPU/GPU 一致替代科学验证。
新运行 H5/plt/checkpoint/完整数组/trace 留本机持久目录；只提交经过检查的
汇总指标、逐次计时表、必要图表、诊断摘要与本地数据索引。
每阶段更新本清单并提交，维护者 review 决定合并。未知/失败/未验证状态如实保留。

导入检查：旧 target、第三方许可证和 round-trip fixture 自带尾随空白；整棵新增子树的 diff-check 报出这些历史字节。为保持封箱子树及测试原文，未格式化它们。相对 c96e9da0 的 Studio diff-check 与本轮新增清单的 diff-check 分别通过。

本轮详细证据及未完成项见 [3B Linux 复验](../../studio/STUDIO_3B_LINUX_REVALIDATION.md)。

## 配置 v3 候选契约（非运行实现）

入口为 src/api/CONFIGURATION_V3_CANDIDATE.md；共享样例位于
src/api/examples/configuration-v3-candidate/。94-key 目标目录继承现有 Core 的
单位/选项/展示元数据，按唯一计划限制默认集合；当前生产 binary 仍发布 v2 和 95 keys。
四组完整 JSON 封套覆盖有效 Sod、空输入、缺少 burn 开关、语法/重复错误。
Sod 成功样例覆盖 94 标准项、7 case 项及 log_dir；完整性限于静态声明检查。
缺少开关不解析为 false；解析值、默认解析结果、派生值分开记录。

检查命令：python3 src/api/examples/configuration-v3-candidate/verify_candidates.py。
此检查只证明候选数据内部一致，不证明 ARCH 已执行 v3。接下来迁移 Core 输入记录、
共同条件解析及受控构造，再将真实输出接入 Host/Studio；不重复已通过的未变更 3B 检查。
其余模型声明、case-defined 例子、错误/预算/身份矩阵随实现补齐并运行真实契约回归。
