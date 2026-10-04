# O7 恢复实施：限定 CPU 短 gate checkpoint

依据：Core 11a321d5604f9ee62b9f9587c81f14de4f128bc4，JeansRZPlatformHandoff 第2.1节。前一 review checkpoint 2a348c100a7e8ccad4997091f1828c8c6222a8f0 已推送。平台为14700K＋RTX4070Ti；旧3B限制不再阻止这里的获准 Core 修改。

## 修复与权威边界

- CartesianPoisson 的公共 validate_mesh 保持物理 Cartesian spacing ratio≤2；共享 geometry 校验仍覆盖有限/正值、索引与算术容量、曲线坐标域。
- CompositePoisson 新构造入口为 private，只有 CompositeMultigrid friend 能从实际 fine operator 构造。验证相同维度、几何、语义、origin、boundary 和 domain；只接受受控 dyadic 变化。AMR leaf level 与公共 bool 不授予豁免。
- 内部粗化继续到实际 bottom≤64，不提前截停。原 child-volume 对照、面 stencil、边界/nullspace、dense bottom、共享 Host/CUDA assembly 与 FGMRES 未更换。
- 新契约覆盖(32,8,4)、(64,16,16)，Periodic/Dirichlet，uniform/mixed，共8组；重复求解与不同拓扑构造。公共入口物理 ratio=4 与非有限几何仍拒绝，private 构造有编译期不可访问检查。
- radial_1d.regrid_cycle 仅迁移 Core 指定的非反应/无扩散单组分 IdealGas 样本。删除可写 gravity_G 输入，rho0=1e7*(1e-20/6.67430e-8)=1.4982844642883896e-6。保留 T、cv、gamma、domain、速度、CFL、AMR 阈值、边界、终点与相对验收预算。P 从共享 EOS 的 rho/T 初始化同比缩放；质量和流体能量同比缩放，不能认为所有绝对量不变。
- 未改科学公式、默认生产 G、EOS 或 checkpoint 身份。不推广到 Helm/reactive/transport；负例拒绝这些扩展和重叠 density floor。

## 结果与覆盖范围

| 检查 | 结果 |
| --- | --- |
| 内存保护 CPU build，28 parallel | PASS；首次31步骤，最低available约19.5GiB，swap增长0 |
| 原self-gravity quick suite | PASS，33条验收记录；全新本地目录，旧失败证据保留 |
| 原受影响 CTest | 6/6 PASS：gravity stage、uniform Poisson contract/analytic、composite analytic/contract、self-gravity lifecycle |
| coarse 8组 | PASS，原1e-10相对 residual控制，不替代独立物理误差gate |
| 原 native-mixed-3d | PASS；10次重力solve，质量漂移1.1933e-16，能量/初始势能4.6191e-6 |
| spherical regrid | PASS；Gauss最大8.1756e-9<1e-7；质量2.9484e-15、能量1.9533e-15<1e-12 |
| cylindrical regrid | PASS；Gauss最大3.9725e-12<1e-7；质量2.9896e-15、能量2.0034e-15<1e-12 |
| 两个 regrid 生命周期 | refine/coarsen/unchanged均存在，162 solves/case，floor repair=0 |
| 实际 initial ENER/rho | 保持 cv*T；最大相对误差4.4409e-16/2.2204e-16，density高于原floor |
| 限定迁移契约 | 3/3 PASS；4类不支持迁移在模拟前拒绝 |
| tracked-source architecture | 仍FAIL，只有原两条finding；候选尚未应用 |

mixed-3d 保留原 max_steps=2，实际终点0.0005208006218809392；未到tmax=.02。该结果仅为原短样本，不是完整长跑或完整科学验收。新增 coarse 残差是独立重新apply算子的代数检查；既有解析phi/force/order预算由原composite analytic回归保持，不据此冻结新的JENS或RZ科学阈值。

二进制SHA256：e9d5cf22f654c12dae450e921244a86181acef5f01ce0ec460b2bc0e89f37b72。构建发生于源HEAD 2a348c10 加本次列明working-tree输入；处理后JSON包含精确源码fingerprints，不冒称 clean HEAD binary 或完整Build Manifest。未替换既有 Studio managed binary/Manifest。

## 复现和数据保管

处理后的输入、build identity、指标及范围见 validation/gravity/results/o7-resume-20261004/summary.json。复现使用已有 NumPy/h5py 环境：

    python validation/gravity/run_o7_resume.py --arch build-cpu/bin/ARCH --output <new-local-persistent-directory>
    python tests/host/gravity/test_radial_similarity.py -v

复现入口拒绝复用已有输出目录。原始H5/plt/checkpoint、trace和全量日志在本机 studio/.local/integration/coarse-resume-20261004，不提交。已有旧失败记录保留。首次新test诊断换行误写为多字符常量，已修正后重编译；一次system Python因缺h5py在执行模拟前退出，改用已有venv后通过。直接repo audit扫描ignored历史候选树产生重复finding；tracked-source快照复核只剩原两条，不用它伪造PASS。

## 后续职责

架构规则ID、职责迁移及候选见 O7ArchitectureRulesReview-20261004.zh-CN.md / O7ArchitectureRulesCandidate-20261004.patch，仍供Core review。JENS/RZ参考、采样含义和预算来源逐项见 O7JeansRzReviewQuestions-20261004.zh-CN.md；不自行猜定。

恢复原计划其他Core/API/Studio工作；CUDA与独立三维科学覆盖继续按原计划。已通过对应短gate且输入/物理终点预算冻结的模型可长跑；本次mixed-3d的两步结果本身不提供新的长跑预算。尚未确认的RZ有限环参考、near/far误差分配和Lz finding单独等待，不再暂停所有工作。没有Windows适配、main merge或完整科学验收标签。
