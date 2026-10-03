# Build 前后工具链稳定性证据

2026-10-03，基线 ad6ddb899de85d2880fa88e2dada7710c85cd61a。

Host 在标准 Build spawn 前采集当前 CMake compiler driver、GNU components 与 specs，成功 Build 后再次采集。Manifest 保留 preBuildCompilerDrivers 与 compilerDriversStableDuringBuild；比较语言、compiler 身份、路径/realpath、SHA/size、组件角色及 specs hash，忽略数组顺序。缺少任一端不伪造稳定结论。

两端变化即使 post-Build 与当前磁盘一致仍 needs-build。缺失旧证据保持 freshness-unknown；已确认当前文件漂移仍优先 needs-build。Manifest loader 校验前快照与布尔字段，不回写历史成功 Manifest。

回归通过：17 项 Build/CMake 定向检查；最终完整 Studio/Host 234/234、lint、typecheck、production build、diff check PASS。新测试在 spawn 边界确定性更换 fixture driver，验证成功构建状态与 provenance readiness 分离、重载仍 needs-build、下一次稳定快照、旧证据 unknown 和已知漂移优先。真实 GNU 固定查询在已有回归中核对。测试仅使用临时工程和 fixture，未重新编译 ARCH。

日志留 studio/.local/integration/toolchain-stability-final.log。dist 不提交。当前运行 Host 尚未重启，本轮不宣称真实 ARCH Manifest 已包含新字段。

限制：前后采样不能发现构建期间修改后恢复的 ABA 变化，不是不可变工具链隔离；每次编译实际参数、隐式库及完整依赖覆盖仍未证明。dependenciesComplete=false 保持不变。当前实际 linker depfile 中已消失的 LTO 临时文件仍使链接覆盖不完整。本项不代表整个 3C/O7 完成。

不修改 Core、科学公式、阈值、架构审计或历史 G 输入；无 simulation、Preview、CUDA、push/tag/main merge。
