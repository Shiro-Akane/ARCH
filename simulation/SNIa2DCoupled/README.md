# SNIa2DCoupled: 二维四模块联动示例

本算例以 **CGS** 单位设置 50/50 `c12/o16` 燃料和光滑温度热点，并通过正式
Driver 同时运行 Hydro、自引力、aprox13 核反应和热扩散。源文件只引用
`<UserInterface.h>` 与 `<GlobalDefs.h>` 两个 ARCH 公开头；后者公开
`arch::constants` 中的 CGS 常数，算例不需要 `src` 内部头文件。

这个配置参考本机 FLASH 4.8 `source/Simulation/SimulationMain/RTFlame/flash.par`
的 C/O 燃料概念，不移植其火焰模型、静水平衡、边界条件或空间尺度。ARCH 示例
采用 1 cm 周期方盒、16×16 网格、120 步，用途是检查四模块联动和 CPU/CUDA
运行路径。二维周期 Poisson 解代表平移不变的二维模型，**不是**三维孤立白矮星的
自引力。没有 AMR 层级，也不以这个示例声称 SN Ia 物理可解析性或爆轰收敛性。

从仓库根目录运行：

```sh
./build-cpu/bin/ARCH SNIa2DCoupled simulation/SNIa2DCoupled/SNIa2DCoupled.par
```

上例使用 `cpu-release` 预设的程序；CUDA 使用 `cuda-release` 的
`./build-cuda/bin/ARCH`。默认使用 CPU。若要用 CUDA，复制参数文件并将 `compute_backend=cuda` 加入副本，
同时为两次运行分别指定 `out_dir`。表格和重现实验记录见
[`validation/gravity/results/snia2d-20260923/README.md`](../../validation/gravity/results/snia2d-20260923/README.md)。
运行结束应看到 120 个接受步、`state_repairs.txt` 中 `events=0`、
`gravity_solves.tsv` 中每次 `residual <= target`，以及最终 plot 中的
`GPOT/GACX/GACY` 与 C/O、温度、能量场。`dt_diff` 为有限值表示热扩散调度
已参与步长估计，但这份冒烟测试不测量独立扩散误差。

Helmholtz 路径在此配置下只验证**热扩散**；不要把 `use_species_diff` 设为已验收的
物质输运。核反应使用有效 EOS 表 `EOS_toolkit/tables/helmholtz/helm_table.dat`，
没有此表时运行会明确失败。热点温度为 $T=T_0+(T_{\rm peak}-T_0)\exp[-r^2/(2\sigma^2)]$；
密度使用同一热点函数的 1% 扰动，因此周期
自引力既有非零源，也避免均匀模式造成虚假的二维势。
