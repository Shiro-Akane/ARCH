# 数值与接口代表检查（R2.2）

2026-10-12，`compute/optim`，调查起点 `34a6ccd5f409ec14ec11c4b798e5795e7592f26f`。进度统一由[主计划](../../../../ARCH_MODERNIZATION_PLAN.md)维护；原范围与标准见 [R2.1 基线](BaselineIdentity-20261012.zh-CN.md)。**本文件保留R2.2原批次快照：CPU 27/27、CUDA 9/9、十个应用case及三组收敛通过；当时三项条件CUDA owner因补建预算后置。其后[R2.3](R23PerformanceAndConfiguration-20261012.zh-CN.md)已补齐三项，当前CUDA合计12/12，整个R2按既定代表范围完成。原失败／截断和机器记录保留。**

## 已完成范围

| 组 | 实际执行 | 当前结果与范围 |
| --- | --- | --- |
| G1 Hydro | PPM 数学、保守通量限制、保守性准入、共享阶段调度，共4项 | 4/4；共享数学与调度 owner，不代表所有 flux／EOS 路由 |
| G2 EOS／burn／NSE | Helm 电子／光子组分、native tabular 制造解、burn mainline reference、generated NSE，共4项 | 4/4；burn 原 owner 的时间参考／负对照保持原断言；未另跑短时应用 replay；native tabular 默认命令不含外部真实 EOSDriver 文件 |
| G3 AMR／checkpoint | 同层交换、通量表面计划、拓扑事务、checkpoint兼容、时间比较，共5项 | 5/5；兼容 owner 为原读写／身份断言，时间比较是内存数据契约；未新增完整 Device fresh campaign |
| G4 RKL | 修复权重 owner | 1/1；应用解析与设备对照另见下表 |
| G5 重力／RZ 边界 | Poisson和composite的contract／analytic、self生命周期、physics quick、RZ runner contract，共7项 | 7/7；composite为原`ci`缩减模式；quick含60条验收记录。RZ runner测试与短quick不等于旧RZ长程通过 |
| G6 配置／API | configuration API／entry／v3、preview API／full model／cellular，共6项 | 6/6；原生产CPU ELF＋当前工作区脚本，未做真实GUI验收或Cellular燃烧速率验证 |

本轮按既定六组代表集执行数值与接口检查。CPU 检查为 27/27，失败和跳过均为 0。通过范围包括共享数学 owner、EOS／burn／NSE、AMR／checkpoint、RKL、重力 quick，以及配置和预览 API 契约。owner 通过仅授予其实际覆盖的范围，不能代替完整耦合轨迹或真实 GUI 验收。

CPU测试先完成，随后执行应用双后端代表与设备补查。所有原断言、物理终点和误差门槛保持。

## 条件设备补查

| 组 | 原检查 | 结果 |
| --- | --- | --- |
| G1 | `hydro_leaf_parity`、`cuda_conservative_acceptance`、`cuda_ppm_limiter_math` | 3/3通过；原叶函数断言，PPM含450个stencil见证 |
| G2 | `native_tabular_device`、`generated_nse_device`、`network_nse_device`、`burn_controller_parity` | 4/4通过；前三个原缺失目标已定点补建，native表仍为原制造夹具范围 |
| G3 | `cuda_amr_exchange`、`cuda_regrid_transaction` | 2/2通过；原设备交换／事务范围，未授予完整fresh重启 |
| G4 | `diffusion_rkl_parity`、`cuda_backend_diffusion_rkl1`、`cuda_backend_diffusion_rkl2` | 3项后置；前者补建截断，后两者所需`arch_cuda_hydro_block`未开始补建。未运行过期ELF，没有把跳过记通过 |

R2.2原批次实际执行的CUDA检查共 **9/9**，失败、跳过均为 **0**；三项后置单列，不把当轮“9/9”写成全部12项完成。RKL1／RKL2的原应用层解析、守恒、有界性和CPU/CUDA对照已经通过，与叶函数owner后置分开。三项后续通过的执行物和命令由R2.3另行记录。

## 应用解析参考与 CPU/CUDA 对照

四个独立批次共 **10个原case、51对CPU/CUDA checkpoint比较**。每个case保留原accepted_steps，再到精确物理终点；参数哈希、backend plan、CUDA trace、checkpoint metadata及运行后身份均通过既有runner检查。

| 原代表 | 解析／独立参考与实际结果 | 原门槛及结论 |
| --- | --- | --- |
| PPM平滑波 N=64/128/256，t=0.1 | CPU L1分别2.472783e-7／1.547626e-8／9.676764e-10；末对L1阶3.999388；GPU相同指标 | L1≤1e-4、末对阶≥2.7、质量漂移≤1e-12、正性；后端2e-10／2e-12。通过 |
| Sod N=64/128/256，t=0.2 | 密度L1分别0.004583412／0.002423119／0.001112692；L2分别0.009999176／0.006591740／0.004096282；两对L1阶0.919557／1.122810，L2阶0.601150／0.686344；最大激波位置误差0.427593格 | 各档原L1／L2门槛0.05／0.10、0.03／0.08、0.02／0.06；每对阶≥0.7／0.3、正性、激波≤2.5格；后端5e-9／5e-12。通过 |
| RKL1 N=64，t=0.1 | L1=3.282198e-6，Linf=5.147386e-6，均值0.5000000000000001，分数范围约0.260058–0.739942 | L1／Linf≤1e-5、均值漂移≤1e-12、有界0–1；后端5e-10／2e-12。通过；单档不授予收敛阶 |
| RKL2 N=64/128/256，t=0.1 | CPU L1分别4.850034e-6／1.212787e-6／3.032140e-7；CPU／GPU末对阶均约1.999917 | L1≤1e-5、末对阶≥1.8、均值漂移≤1e-12、有界0–1；后端5e-10／2e-12。通过 |

G5 Jeans quick也达到原t=0.1，密度相对RMS为0.00159578848≤0.02，质量相对漂移2.910383e-17；完整60条quick结果仍按各原子检查的范围解释。

这里的“对照通过”使用原manifest指定字段和rtol／atol；它不宣称整个状态逐位相同，也不能代替解析／独立参考。RKL案例仅代表Ideal常系数species扩散，不授予完整Native RZ耦合。

## 本轮发现与处理

| 事实 | 本轮处置 | 后续落点 |
| --- | --- | --- |
| 六个旧Hydro输入缺`hll_wave_speed`／`min_eint`，原配置检查退出3；首个应用CPU启动退出1 | 保留原输入及失败；派生manifest仅加入历史`roe`／`1e-10`，来源为`c8f7c048ca7b384126547934946a3c4d595f6e99`的`StandardParameters.h:53`及`GlobalDefs.h:80`。原输入字节保留，原方法、终点、预算未修改；派生重放通过单独记录 | 阶段4修复活跃验证输入的显式声明；旧输入自身仍是配置失败，不被派生PASS覆盖 |
| 指定`--case`时，runner把`resolution_groups`写为空 | 对PPM、Sod、RKL2完整三档结果显式调用原`validate_resolution_groups`，三份回执关联manifest与原运行证据哈希；没有修改runner | 阶段4补齐选择case时的收敛入口及相关验证；不降低门槛 |
| CUDA缓存的host checkpoint比较器仍拒绝revision3、要求revision2 | 首次RKL1在step1比较失败，原日志保留。备份原ELF后，以原参数定点补建原目标两个C++单元，并更新原配置路径；原CUDA生产ARCH和缓存配置保持。新比较器SHA见机器记录 | 后续恢复时先核对辅助执行物，不能只核主ARCH或CPU同名owner |
| CPU self生命周期owner的GravityStage对象早于当前源码；部分CUDA owner也过期或缺失 | CPU原目标三个单元在新目录补建后通过；设备目标仅定点补建并记录新身份，不完整重编CUDA | 源码树、对象依赖和实际执行物分别绑定；编译成本仅作本轮预算诊断 |

## 身份、复现与证据

- 当前Core区域与R2.1冻结树一致；CPU owner主执行物来自`build-release-cpu`，应用两条lane使用`build-rt-cuda/bin/ARCH`的显式`cpu`／`cuda`后端。旧主checkout仍为`9aaa5add…`，另记与当前目标的Core树相等，不能把文档提交当旧执行物来源。
- CPU及CUDA生产ARCH仍分别为`a1c76c6…`、`0828b3c…`；CUDA比较器原`9ccfbdca…`备份在本轮`retained-originals/`，新`d7e7df39…`。完整哈希和本轮命令由机器记录索引。
- 为保护原build-tree夹具，使用输出隔离的本地CTest清单；只改输出路径、当前等源码脚本入口和有依据的owner执行物。原命令、实际命令、原属性／跳过语义均保留，重负载串行。
- 本机证据根为`output/modernization/r22-20261012/`：CPU各组`commands.json`／`junit.xml`／`guard.log`，四个应用批次的`backend-validation-evidence.json`，三份`convergence-receipt.json`，两份原失败、补建命令及旧比较器原字节均保留。该目录忽略提交；本轮651个H5合计80,589,096字节（约76.9MiB），不进入Git。

复现时先按[基线入口](BaselineIdentity-20261012.zh-CN.md#后续复现入口)核对源码、表和本轮更新的辅助执行物。Hydro须从原manifest生成派生副本，仅对上述六个ID加入`overrides={hll_wave_speed: roe, min_eint: 1e-10}`；不能把全局默认重新加回生产配置。原CLI选择完整三档并写新目录后，导入`tools/validate_backend_results.py`，以同一manifest选出的cases与实际`evidence["cases"]`调用`validate_resolution_groups`。完整可执行命令与输入／派生manifest哈希保存在本轮机器记录及本机配方中。

## R2.2当轮停止边界与历史接续

R2.2当轮不重复旧 RZ 长程，也不自动进入 R2.3。该批次实际运行耗时仅用于执行预算与异常诊断，不作为正式性能基线。没有运行旧RZ五次穿越／fresh、旧全Hydro耦合长矩阵、Cellular速率复现或完整CUDA生产编译。

H5批次前实查宿主E盘剩余23,499,665,408字节，约21.9GiB；沿用约余1GiB预警，仅批次前、显著增长或I/O异常时复查。科学运行均未触发护栏或时间截断。可选设备owner补建在600.021秒观察上限被SIGTERM受控停止，外层退出124；已完成4个目标，停在RKL parity的nvcc编译，未记PASS。该批次观察峰值owned RSS为4,602,252KiB，Swap从0增至1,292KiB，未触发原资源阈值。其后仅独立补齐三个缺失EOS/NSE目标，用时325.893秒、退出0；未重试已截断目标。以上是预算诊断，非正式编译／RAM基线。

收尾机械检查通过：CPU／CUDA／后置名称分区完整，十个case与51对比较的原身份／端点一致；67份本机处理后证据哈希核对，107个修改文档本地文件链接无缺失，表格无孤立行，生产／测试源码Git差异为零。

当轮交接建议是在授权R2.3后用已接受的PPM／Sod／RKL代表测运行、编译和RAM成本，避免测量变成重跑长程；现已按[R2.3收尾记录](R23PerformanceAndConfiguration-20261012.zh-CN.md)执行。活跃输入声明、选择case收敛入口和执行物身份维护仍列阶段4候选，阶段3排序后再做修改；阶段3未启动。

<details>
<summary>机器证据、协作回执与复现配方</summary>

[R2.2机器记录](../../modernization/R22NumericalChecks-20261012.json)保存当前树／ELF身份、27项CPU及9项CUDA owner、十个case指标、三组收敛、原失败／补建截断、实际命令与本机处理后证据哈希。原输入／R2.1快照保持，当前主计划只索引本结果，不复制另一套进度表。

本机`executed-application-commands.json`为四个成功批次的实际命令；`hydro-replay-controls.json`给出派生manifest来源；各组`commands.json`保留原／实际CTest命令；`focused-*/commands.json`与`focused-build-recipe.py`保留原目标原flags的定点补建。新输出目录须为空，重负载继续使用现有护栏串行。旧CUDA比较器原字节在`retained-originals/`；本轮机器记录提供该备份及新ELF的完整SHA-256。

DeepSeek：`081009ca…`（G1来源）、`49dc22c4…`（owner与输出）、`73641f57…`（实际指标独立计算）限定证据采用；收敛入口调查首轮含路径／范围误判，一次合并返修`ad355fa2…`虽有格式前缀失败，但完整原文保留，仅采用Codex已核实的段落。Gemini `7b267a35…`仅编辑冻结文字；哈希、数值／代码锁和语义核验后采用首尾段。没有委派原始H5，也没有修改全局Skill／MCP。后续无需为已通过代表额外付费重复审查；新的数值失败或范围变更才触发复核。

</details>
