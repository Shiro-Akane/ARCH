# Linux／WSL 3C 阶段出口审计

2026-10-03。基线 e240bbd92e651c31005a9d9b2bba08fc52e62ba2，开始前工作树clean。
依据StudioConfigurationHandoff.zh-CN.md第4.1、4.2、5、6节。

**主链路已有直接证据，完整出口仍待并发隔离场景补证。**

| 要求 | 状态 | 证据与边界 |
| --- | --- | --- |
| Linux独立production窗口 | VERIFIED | StudioNativeCurrentHostRelaunch.zh-CN.md；Linux entry执行Linux Electron；用户已提供可见窗口截图，正常流程无需npm/端口/browser。 |
| 原生文件选择/SaveAs/Reopen/Dirty保护/外部冲突 | VERIFIED | StudioNativeFileLifecycleUat.zh-CN.md；空格/中文路径字节操作通过；中文缺字形仍是展示问题。 |
| 明确overwrite/取消保留/落盘重读 | VERIFIED | StudioNativeOverwriteUat.zh-CN.md；独立测试副本唯一cfl修改，原始输入及binary不变。 |
| 原生Configure | VERIFIED | StudioNativeConfigureUat.zh-CN.md；真实CPU Configure成功；host-configure.test.ts另有真实CMake失败恢复/owned-group取消。 |
| 原生Build及真实编译失败恢复 | VERIFIED | StudioNativeToolchainBuild.zh-CN.md；原生ARCH Build无工作增量；host-build-real.test.ts真实微型C++成功/失败/恢复，不是ARCH干净编译。 |
| 输入/工具链/binary/构建新鲜度 | VERIFIED_WITH_LIMITS | StudioBuildToolchainStability.zh-CN.md；735编译依赖/66objects及driver前后证据；link不完整继续unknown，不谎称current。 |
| 原生Run及独立终端 | VERIFIED | StudioNativeActiveRunCloseUat.zh-CN.md；小型CPU Sod，显式compiled-version确认，不替代独立科学验收。 |
| 真实Restart继续演化 | VERIFIED | validation/backend/results/studio-native-sod-restart-20261003/summary.json；t0.05/step67→t0.2/step280；既有21datasets/maxAbs0。当前job/state/input/binary只读复核一致。 |
| 关闭Studio/Host后Run存活/恢复 | VERIFIED | StudioHostCloseSurvivalUat.zh-CN.md；真实Host关闭后观察活跃进程/日志增长；另有native close后9.370秒Run成功。 |
| owned Stop/无关进程隔离 | VERIFIED | StudioNativeStopUat.zh-CN.md；原生Stop得到stopped/SIGTERM；host-run-worker.test.ts覆盖wrong ID/unrelated process。 |
| pending Preview/AMR close cleanup | VERIFIED_WITH_LIMITS | StudioNativeStalledAmrClose.zh-CN.md；与StudioNativeStalledPreviewClose分别以owned-worker stall/guard验证，不声称自然数学调用瞬间覆盖。 |
| 活跃Run与Preview cancel/不同项目重开 | PARTIAL_DIRECT_EVIDENCE | studio/host/desktop.ts；Run独立所有权代码及close survival已证明；同时取消Preview/实际重开不同项目的同场景直接证据仍缺。 |

## 新增真实构建失败覆盖

旧Build negative tests使用fakeSpawn。新增studio/tests/host-build-real.test.ts：
在空格/中文临时目录，以真实ConfigureRunner/BuildRunner/CMake/Ninja编译微型C++。
成功→明确#error编译失败→修正成功。失败必须保留上一成功Manifest/binary字节/错误输入，
latestResult准确failed，tracked source变化给needs-build；恢复更新Manifest ID。
dependenciesComplete=false仍unknown。fixture不包含ARCH科学Core，不证明ARCH干净全编译。

定向1/1、最终Studio/Host235/235、lint/typecheck/production build/diff check PASS；已有bundle warning保留。
完整日志本机ignored；不新增镜像实现的测试，不重复Host子集计数。

## Restart当前身份复核

run30db4e5c的持久job/state与报告一致，saved/frozen input逐字节一致；
当前输入和CPU binary SHA符合原报告，checkpoint及双方最终H5仍保留本机。
不重新演化或重复未变化的21datasets完整对照；maxAbs0仅证明该CPU续算一致。

## 精确待补证项

联合要求Preview取消/项目切换不能杀delivered Run。Run已交给独立终端/worker，
Host close不调用Run清理；真实Host/Studio关闭生存已有直接证明。
Desktop目前拒绝热替换已拥有项目，支持先关闭再打开另一项目。
仍待同一活跃Run期间取消Preview，以及关闭后实际启动不同项目的直接隔离记录。
下一步只补此场景，以现有小型CPU输入或明确OS fixture区分覆盖，不扩大科学终点。
不为自然短Init瞬时关闭新增无限重试门槛，既有fault injection的scope保持准确。

## 阶段边界

历史非物理G输入迁移仍需Core负责人确认；architecture-audit迁移提案仍未获准应用。
全模型/plt、JENS/RZ、CUDA和批准长轨迹均未完成。
字体缺字形、确认需要滚动等显示问题保留，不转向Windows适配。
本轮仅测试/精简审计；无simulation/ARCH build/CUDA、push/tag/main merge。
raw/log保留studio/.local/integration/3c-exit-audit-20261003；提交不含H5或场数组。
