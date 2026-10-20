# 第二平台冻结输入就绪审计

当前阶段：联合计划第8项，准备正式第二平台计时包，不启动新O9轨迹。
结论：只读审计完成；正式benchmark仍待维护者冻结，不宣称性能验收。

## 当前 executable / 已通过出口

CPU ELF32f7b13972ac9381076b06e04c581900065a246796e911bdd161479d86f31b8e：
冻结JENS9+9、原主入口80记录PASS。
CUDA ELF1cbbd6952f4970f89ef5071b9e3f18d9ec619ec92824f0ad9928455f6e27eb53：
原批准Wave/Box/径向74记录、58实际Device run PASS。
完整RZ和public CUDA JENS尚未签收；不能用上述子组为任意新组合补预算。

## 八份候选只读真实结果

当前CPU --inspect-config SNIaCoupled --config-stdin，核对case/request/config SHA。
响应明确Setup未执行、EOS未加载、CUDA未初始化、文件未访问、simulation readiness未检查。
另由Host按真实cwd核对schema标明的声明input-file，记录size/hash；
这不是模型最终加载身份或Setup成功。

| input | exit | declared completeness | diagnostics |
| --- | --- | --- | --- |
| SNIaCoupled_2d_cartesian.par | 0 | complete | 0 |
| SNIaCoupled_2d_cartesian_amr.par | 0 | complete | 0 |
| SNIaCoupled_2d_polar_amr.par | 0 | complete | 0 |
| SNIaCoupled_3d_cartesian_amr.par | 0 | complete | 0 |
| SNIaCoupled_3d_cylindrical_amr.par | 0 | complete | 0 |
| SNIaCoupled_3d_spherical_amr.par | 0 | complete | 0 |
| p13_polar_origin_4x4_amr.par | 3 | incomplete | 16 |
| p13_polar_origin_4x4_regular.par | 3 | incomplete | 16 |

PLATFORM-P13-CONFIG-01 OPEN：两份旧P13各缺16键：
diff_cfl, enucDtFactor, eos_coulomb_mult, gravity_atol, hll_wave_speed, max_eint, min_eint, nuclearDensMin, ode_atol, ode_rtol, smallt, smallx, sml_rho, use_species_diff, use_viscous_diff, center_z。
不从前端、其他样本或猜测默认补齐；应由Core指定历史控制迁移。
既有模拟输入已显式迁移，但其声明完整不等于Setup/physics-ready。
当前API对二维cylindrical声明(r,phi)/rad；与新内部RZ(r,z)不同，
旧polar样本不会被作为新RZ benchmark包。没有改变单位、域、模型或物理。

声明EOS表存在，size60242514 bytes，
SHA-256 c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1。
只hash原文件，不加载EOS、不复制/上传表；双EOS路径语义保持独立。

## 现有计时工具及仍缺材料

run_cuda_matrix.py已支持同CUDA Release binary选择CPU/CUDA、
另一个CPU-only baseline、物理终点max_steps=-1、完成后核验、
readonly冻结输入、显式线程/taskset、独立预热和至少三次交替配对。
现有qualifed_benchmark=false保持；不另建机器专用runner。
Standalone OpenMP平台观察已有1/8/16/20/28 actual team/affinity证据，
不冒称生产ARCH线程筛选或P/E核识别，不重复未变工程探针。

正式包请Core明确：
1. 选定case与准确完整.par SHA；首选范围按计划为Cartesian 2D/3D AMR，
   曲线代表项单独指定，旧二维polar不替代新RZ。
2. 短benchmark及长轨迹各自的t_end、域/分辨率、CFL、AMR/regrid、输出/采样时刻；
   原max_steps=3/5等smoke不是已冻结完整终点，30–60分钟不是物理终止条件。
3. EOS/network数据身份、独立参考/守恒与源边界收支、原预算和CPU↔CUDA对照判据；
   Helmholtz/四模块不自行按IdealGas低G相似规则迁移。
4. 完整计时前的对应短gate及CPU↔CUDA续算代表checkpoint，
   资源/磁盘/墙钟边界；未冻结新RZ子集继续隔离。

包确认后，先做有限生产线程/亲和性筛选并完整保留结果，
冻结CPU与CUDA Host资源，再串行预热/交替计时，记录负收益和unavailable硬件指标。
不得从已观测并发秒数倒推speedup或只取最快一次。

## 复现和交付

validation/gravity/curved/audit_platform_inputs.py
  --arch build-cpu/bin/ARCH --source-root REPO --case SNIaCoupled
  --input CANDIDATE.par [--input ...] --output NEW_LOCAL_DIRECTORY

处理后summary：validation/gravity/results/platform-input-contract-audit-20261005/summary.json。
完整API响应/stdout/stderr保留studio/.local/integration/platform-input-contract-audit-final-20261005/；
初次探查记录独立保留，未覆盖。原始H5/plt/checkpoint/EOS不提交。
本轮没有simulation、Setup、Preview、configure/build、输入更改或科学gate放宽。
