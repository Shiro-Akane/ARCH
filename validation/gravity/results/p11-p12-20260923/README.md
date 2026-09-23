# P11–P12 多维曲线坐标自引力：CPU 验证与 FLASH Cellular 对照

状态（2026-09-24）：**P11 的避奇点 CPU 域已完成本轮椭圆与混合 AMR 验收；P12 仅完成标量椭圆奇点检查，尚未开放多维奇点生产运行。**
源码为 `physics/selfgravity` 工作树，起点 `d9e61f88`；CPU 程序为
`build-ci/cpu/bin/ARCH`，Release、OpenMP 16 线程。FLASH 数据来自用户现有的
`/home/shiroakane/FLASH4.8_code.tar.gz`：二维使用归档自带的 Cellular `flash4`，
三维使用同一现成算例从归档重建的程序；未编写或修改 FLASH Fortran。原始数值输出保存在 `/tmp`，
本目录保留可复查的简表、独立比较脚本与验收记录，不提交大型 plot/checkpoint。

## ARCH 的受测能力

P11 开放的组合是 CPU、`gravity_boundary=isolated`、完整 `2π` 方位角和正内半径：
二维 Grid 原生 `(r,φ)` 极坐标代表单位长度质量的对数势；三维柱 `(r,z,φ)`
和三维球 `(r,θ,φ)` 使用有限质量 Newton 势，其中球域的 `θ` 避开两极。
`CompositePoisson` 以共用 `GridMetrics` 的精确单元体积和面面积构造通量；
物理空间质量树生成孤立边界，原有 `CompositeMultigrid` 求解。动量取原生正交
加速度，能量仍由同一个质量通量面做功入口处理。二维/三维均使用原有
`SelfGravity` 生命周期和 AMR adapter，没有坐标专属源项或新用户数值旋钮。

| 检查 | 受测范围与结果 |
| --- | --- |
| 非轴对称制造解 | 二维极坐标、三维柱/球；均匀与混合 AMR，`n=8→16`；势和物理面力收敛阶均 ≥1.8，最小实际势阶 1.9467、面力阶 2.0414；每次求解残差达标。见 [明细](arch-p11-elliptic-curved.txt)。 |
| 独立孤立边界积分 | 各几何取 12 个物理边界面，与直接单元体积分比较；默认开角的最大相对差 `5.53e-5`（二维），三维最大 `1.26e-8`；开角减半后分别降至约 `1e-12` / `1e-15`。见 [明细](arch-p11-elliptic-curved.txt)。 |
| 解析 Gauss 面力 | 二维圆柱与三维球径向分布，均匀/混合 AMR 的 `n=8→16` 面力阶 1.8878–1.9533。见 [明细](arch-p11-elliptic-gauss.txt)。 |
| 固定物质、扩展外域 | 径向域 `[0.5,1.5]→[0.5,2.5]`，内部网格和紧支撑非轴对称密度保持不变；相同内部面的力 RMS 相对变化 0.089%–0.239%，低于预置 2% 预算。二维势的参考常数会随外半径变化，故比较力。见 [明细](arch-p11-elliptic-domain.txt)。 |
| P12 标量奇点 | 二维原点、三维柱轴、三维球原点及两极，均匀/混合 AMR 制造解的势与面力阶均 ≥1.8；最大迭代 39。该结果**不**验证流体向量跨坐标接合。见 [明细](arch-p12-elliptic-singular.txt)。 |

同一个 `SNIaCoupled` 示例在 CPU 上运行 Hydro、自引力、aprox13 燃烧、
Helmholtz EOS 和 RKL2 热扩散；这是厘米尺度 C/O 热点的**运行与耦合检查**，
不提供 SN Ia 的解析精度主张。五个带 AMR 的输入均有粗细共存，零状态修复、
有限正的扩散限步和非零核能率；每一次 Poisson 求解满足其记录的残差目标。
归档的 [机器可读摘要](arch-p11-coupled-summary.json)包含初末态场范围及逐次求解上界。

| 几何 | 步数 | 末态粗/细叶块 | 最终时间（s） | 引力求解数 | 最大残差/目标 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 二维 Cartesian 全周期 | 5 | 1 / 12 | `1.15552e-11` | 17 | 0.268 |
| 二维极坐标 isolated | 120 | 2 / 8 | `4.72241e-10` | 362 | 0.498 |
| 三维 Cartesian isolated | 5 | 1 / 8 | `1.18766e-11` | 17 | 0.426 |
| 三维柱坐标 isolated | 5 | 1 / 8 | `1.76158e-11` | 17 | 0.462 |
| 三维球坐标 isolated | 5 | 1 / 8 | `1.66682e-11` | 17 | 0.405 |

二维极坐标还做了 10 步连续运行与“5 步检查点 + 5 步恢复”的对照：
时间和网格字段完全相同，23 个物理字段中 22 个逐位一致；只有 `VELY`
的 11/2560 个单元出现近零值舍入差，最大绝对差 `2.07e-10 cm/s`，
相对该场峰值 `1.16e-17`。见 [续算明细](restart-comparison.json)。

## 以现成 FLASH 算例为行业对照

先运行归档内的 FLASH 4.8 Cellular 二维算例，仅把 `nend` 截为 120
步以保留一份原模型轨迹：最终 `t=2.207400769e-8 s`，731 个叶块。
它含原模型的随机扰动与氦 pocket，不能直接拿来计算 ARCH 简化热点的逐场误差。
因此再用**同一个现成 Cellular 算例**建立二维/三维受控对照：只改原 `flash.par` 的
`lrefine_max=2`、`nend=20`、输出间隔 20、域 `64×32 cm`、根块 `8×4`、
`noiseAmplitude=0`、`usePseudo1d=true`、`pocket_mode=4` 和
`pocket_vol_frac_he=1`；三维另设 `zmax=16 cm`、`nblockz=2`。输出路径改到 `/tmp`。
这些都是 FLASH 原有运行参数，完整变换见 [参数生成器](../../flash/make_matched_par.py)。
二维使用归档预编译程序；三维原始 `Cellular` 在本机源码副本中用
`./setup Cellular -auto -3d -maxblocks=512` 重建，通过
`LIB_OPT='-L/home/shiroakane/libs/ma28/ma28-1.0.0/src -lma28'` 接入本地现成
MA28 静态库。较小的 `MAXBLOCKS` 仅使该程序能在本机内存中启动；原 Fortran
源码和算例选择未改。ARCH 端分别用[二维匹配输入](../../flash/arch_cellular_matched_2d.par)
和[三维匹配输入](../../flash/arch_cellular_matched_3d.par)启动现有 `Cellular`，
关闭引力与扩散，以相同流体/燃烧初值隔离这一部分差距。

两端二维、三维第 20 步的物理时刻均为 `1.048575e-10 s`；ARCH 输入的
`dt_init=1e-16 s` 与受测输入一致，比较器会拒绝时间不一致的 plot。使用各自 HDF5 的有效
AMR 叶块，在共同 `0.5 cm` 网格作分段常数重采样；所有像素覆盖恰好一次。
每场误差定义为 `mean(|ARCH-FLASH|)/mean(|FLASH|)`，
算法见 [逐场比较器](../../flash/compare_cellular.py)，
数值见[二维 JSON](arch-flash-cellular-comparison.json)与
[三维 JSON](arch-flash-cellular-comparison-3d.json)。FLASH 第 20 步常规输出和
终止时强制输出分别位于该步重网格前后，物理时间相同；ARCH 末态与两者均比较。

| 场 | 初态相对 L1 | 第 20 步、FLASH 重网格前相对 L1 | 第 20 步、FLASH 重网格后相对 L1 |
| --- | ---: | ---: | ---: |
| 密度 | 0 | 0.335% | 0.335% |
| 温度 | `1.13e-15` | 0.518% | 0.889% |
| 压强 | 0.212% | 0.525% | 1.159% |
| x 速度 | 0 | 0.403% | 0.451% |
| `he4` 质量分数 | 0 | `2.91e-8` | `2.86e-8` |
| `c12` 质量分数 | 接近零，比例无意义 | 3.21%，绝对 L1 `2.86e-8` | 3.21%，绝对 L1 `2.86e-8` |

二维与三维在所列精度上得到相同误差，这是因为原 Cellular 的受控输入启用
`usePseudo1d=true` 且关闭扰动；三维结果检查了三维网格、AMR 和程序执行路径，
**不**构成真实非轴对称三维流动的逐场对照。FLASH 第 20 步重网格前的有效叶块
二维为 56、三维为 176，重网格后分别为 68、232；ARCH 末态分别为 14、22。
初态密度、温度和速度一致，而压强已相差 0.212%；FLASH 与 ARCH
使用的 Helm 表文件及读取格式不同，因此不能把后续差距单独归因于输运、
反应网络或 AMR。此对照只说明受测控制问题的量级，**不**是
四模块自引力算例与 FLASH 的同物理误差，也没有证明真实 SN Ia 的准确度。
三维 Cartesian/柱/球的**四模块**表格仍仅是 ARCH 内部短程耦合验证，
不能把上述 FLASH Cellular 流体/燃烧控制差异直接套在它们身上。

## 复现与未关闭边界

```sh
cmake --build build-ci/cpu -j16
ctest --test-dir build-ci/cpu -R '^composite_poisson_(analytic|contract)$' --output-on-failure
build-ci/cpu/arch_composite_poisson curved
build-ci/cpu/arch_composite_poisson domain
build-ci/cpu/arch_composite_poisson gauss
build-ci/cpu/arch_composite_poisson singular
./build-ci/cpu/bin/ARCH SNIaCoupled simulation/SNIaCoupled/SNIaCoupled_3d_spherical_amr.par
```

其余示例输入在同一目录；验证脚本
[`verify_coupled.py`](../../curved/verify_coupled.py)读取指定运行目录，
核对 AMR 层级、接受步、修复、求解残差及物理场。FLASH 对照用参数生成器
读取归档内 `source/Simulation/SimulationMain/Cellular/flash.par`，分别指定
`--dimension 2` / `3`；二维运行归档预编译 `object/flash4` 的独立副本，
三维按上面的设置与 MA28 路径重建后运行 `object/flash4` 的独立副本；需要该归档
自带的 `helm_table.dat` 与 `SpeciesList.txt` 放在运行目录。ARCH 匹配控制输入
由现有 `Cellular` 注册算例运行。用 `compare_cellular.py --help` 查看四个
初末态 plot 路径参数，以及三维和 FLASH 末态重网格后输出的可选参数。

P12 还必须在 **Grid/AMR 的共用边界和邻接层** 实现原点、轴线、极点的
跨方位角/极角索引与动量基矢变换，然后验证 Hydro、扩散、reflux、
重网格与重启。当前 `BoundaryPlan` 的 reflecting 仅翻转法向动量，
`AmrTree` 仅作同轴周期 wrap；标量 Poisson 正则性不能代替这些条件。
实际生产配置已验证拒绝二维原点、三维柱轴/球原点与极点、部分方位角扇区。
P13 的 CUDA 曲线坐标数值与性能尚未进行；受影响的重力 NVCC 执行/控制
编译单元已通过，只证明编译兼容。用户无需为 P11 增加新物理控制参数。

本轮相关的 10 项 CPU CTest（复合椭圆、原有 Poisson/MG、引力生命周期、
物理 campaign、低密度与 AMR 操作）均通过。既有拒绝 campaign 中
“所有曲线多维均禁止”的旧断言已按新能力改为**原点**与**非完整方位角**
两条明确拒绝；Cartesian isolated 的两个负例也改为独立输出目录和准确原因。
这些修改没有放宽残差、收敛阶、修复或运行误差预算。
