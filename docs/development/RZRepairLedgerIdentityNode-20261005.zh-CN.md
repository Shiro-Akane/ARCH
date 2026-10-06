# RZ repair 账本测度与 checkpoint 身份节点

本节点基于 47cb34d373afd8184e15514559693cfd2bf4d4eb，沿用科学清单第 8 节的唯一 m_phi=J/W 定义。RZ capability gate 保留；这里的 PASS 是明确的消费者与软件回归范围，不是完整科学签收。

## 实际变化

- RepairBudget / borrowed RepairView 带 explicit ExistingVolume / RzVolumeAngular 身份。普通动量、质量、能量与 species 使用 V；RZ slot 6 是 delta J=delta m_phi*W，单位 g*cm²/s。W 只由 GridMetrics 提供。
- RZ 初始化、CPU cell/stage、RK acceptance/pending 和 Runtime 使用同一身份。空账本仅允许由显式 runtime chart 绑定；非零历史账本不允许改标签。异类合并在 mutation 前拒绝。
- 单元密度差转换先检查 V/W 和 FP64 乘积，失败不写账本。严格 RZ 更新仍拒绝不可解析候选；新账本不授权 floor、加热或改变总能量。
- 实际 native checkpoint 写出前要求账本与 geometry 相符，并记录 repair_semantics=rz-native-V-angular-J-v1。读取 RZ 时该 tag 即使零账本也必须存在且匹配；旧的未知 RZ repair 表示不迁移。Existing/legacy checkpoint 缺省仍是已有 volume 表示。
- DriverIO 文本报告使用 geometry revision 2、明确 state/repair identity 和 angular_momentum_signed；不把体积积分的 phi momentum 冒充 J。HDF state arrays、format 6、FP64 与控制器身份未另增独立演化数组。

## 独立验证与错误行为

使用 V=2、W=5、delta m_phi=3 的精确算术反例：旧 volume slot 为 6，新 RZ J slot 为 15；0.5 加权合并得 22.5。异类合并、非零历史改标、无效/非有限 W、乘积溢出拒绝且账本不变。

实际 HDF/read_chk 在 5 个 mixed RZ leaves 上验证非零 J 账本 bit-exact 恢复。缺失、错误、未来 repair tag，以及零账本但缺 tag 均在 live tree/controller 替换前拒绝。错误 writer frame 不截断已有 checkpoint。真实严格 cell stage 的未解析旋转候选拒绝且 ledger 全零。

实际 DriverIO 的 Cartesian/RZ checkpoint、非零 component 报告、Plotfile 与 Linux buffered I/O failure propagation 通过。4 个实际 internal RK2 checkpoint continuation 保持 41040 FP64 words bit-exact；这些是零 phi 的常轴向工程样本，不代表旋转科学验收。

实际 Root/Populate/EOS/Runtime initialization 与 registered Gaussian 三层初始求积继续通过，模型定义与阈值不变。

## CPU 软件回归与冻结 JENS

现有 build-cpu 增量编译全部目标通过，parallel=28，memory guard peak owned RSS 9407496 KiB、swap growth 0；耗时是受保护构建观察，不是性能 benchmark。首轮多余参数编译错误日志保留本机，已修正。

初次完整 CTest 为 67/71；四个失败均记录。随后只复测修改涉及的 fixture：
- checkpoint/report fixture 显式迁移到新账本身份；
- input_resolution 与 ui_expansion 按当前 active registry 检查全部 keys，明确排除 retired gravity_G，不再永久假定 94；
- shared_stage_scheduler 按已存在的严格 reflux 参数接口核对唯一 callback。

四个失败复测通过，新增 low_density 严格拒绝反例通过；本轮所有 71 个 CTest 已有对应 PASS，未无理由重复完整 suite。architecture audit 和 git diff --check 通过。

实际 CPU ARCH SHA256：b3f2c618d1912f1e2f0ae709dfab1e0af33b3f4302a9063d53dcbd680359cd06
冻结 uniform-lifecycle-1：9 个短演化 + 9 个实际 .01 checkpoint restart 到 .02 全 PASS；质量/能量漂移 0，最大 JENS 相对误差 4.50750606346323e-17，restart 全部 bit-exact。阈值、输入、终点不变；CPU self gravity 不冒充 CUDA 支持。

## 未关闭范围与复现

RZ viscosity、轴邻 force 收敛、完整 ring RHS/residual identity、旋转全消费者科学签收及其后 CUDA/长跑继续按依赖推进；本节点不关闭它们。不增加 Windows 适配，不创建新 worktree，不 merge main。

可复现入口：现有 validation/io/run_driver_checkpoint_geometry.py、run_rz_checkpoint_continuation.py、validation/amr/run_rz_initial_population.py、run_registered_rz_initialization.py；冻结 JENS 用既有 BoxCampaign.jeans_uniform_lifecycle 与 check_jeans_uniform.consolidate。准确输入与源 fingerprint 见 validation/io/results/rz-repair-ledger-20261005/summary.json。

原始 H5/plt/checkpoint、完整日志、ELF 和数组只在 studio/.local/integration/rz-repair-ledger-20261005/；本次仅提交代码、测试和标量处理摘要。
