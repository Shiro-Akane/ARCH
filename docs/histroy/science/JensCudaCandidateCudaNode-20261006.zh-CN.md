# 私有 CUDA JENS 冻结短包真实应用节点
状态：已通过（仅 private uniform-lifecycle-1）；公开 CUDA JENS gate 未开放。
候选 base ccfcef5bc60f4361a808dc1e83c020094ab50930，
delivery ref 11c665b37deb0f73106e2d2ed90d22c27082b836，patch SHA256
c026244a12f55900062aa32f213466d6d07dddf388eb18695424b0f2726bb123。

CPU候选9+9通过后，私有 CUDA Release sm89/nvcc12.8/cuDSS/原KLU独立构建成功。
配置 API、configuration_input、checkpoint_compatibility 三项通过。
真实 GravityBox disabled/output-only/active ×1D/2D/3D，共9次演化及9次
从实际 t=0.01 checkpoint续算至实际t=0.02；strict native state/attrs相同，
off/output 原生场与solve次数相同，active接受宏步事务覆盖完整。
原均匀DENS/PRES/TEMP/ENER/species、零GPOT/GAC/速度，原16epsilon、
mass/E<=1e-12、repair=0、physical residual全部通过。所有gravity solves device=1，
有真实kernels；无Host fallback。逐输入与ELF/cache SHA及scalar记录见cuda-summary.json。

这次有真实演化/输出/续算，区别于此前内部t=0事务fixture；
仍不是非均匀/其他EOS/非线性JENS、完整RZ或性能签收。
候选GPU构建/运行由memory/swap/墙钟及VRAM/产物磁盘poll guard保护；
各上限/实测/退出原因原样提交。GPU统计是whole-device保守限制与观察，
不冒称owned GPU allocation；没有因背景显存使用降低科学输入。
CPU lane磁盘仅测量的限制沿CPU节点如实保留。
所有raw H5/checkpoint/逐cell数组/full日志/ELF留本机。
public Core/API/Driver、原publicCPU/CUDA caches/ELF前后SHA不变。
Core据该有限子集review决定开放范围；本次不提交公开能力修改。

原始目录 studio/.local/integration/jens-cuda-candidate-20261006。
candidate execution依赖原BoxCampaign/check_jeans_uniform；本机identity-recording、
资源监控与dependency continuation脚本 SHA如下，脚本只用于候选：
- run-frozen.py: 5e3ef2beba7811c5c15c2ed685c3f742a5fc03a11c3aa29899bcaa04a3cd44ab
- run-limited.py: 4d857b12492fb4fa1fea34376c2832fff8c45d3adf96f9fc325976bdc590c043
- continue-cuda.py: a92659b38b6d95eb79890deebcc6de2a879acd68b913f486179ce905a0371271
