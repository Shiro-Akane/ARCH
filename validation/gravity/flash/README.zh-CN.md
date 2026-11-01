# ARCH 与 FLASH 性能对照

[English](README.md) · [引力验证](../README.zh-CN.md)

本页比较指定问题的完整模拟成本。参考程序来自带本地初值扩展的 FLASH 4.8
Cellular 应用，输入和程序身份与 ARCH 一起记录。FLASH 为可选工具，ARCH 的构建、
数值测试和 CI 无需安装它。

## 方法与结果解释

比较采用相同物理终点，并记录网格变化、活动物理模块、场误差和状态修复。
ARCH 与 FLASH 在水动限步、面 EOS 求值和燃烧处理上不同：本组 ARCH 使用两个
热耦合 Strang 燃烧半步和完整面 EOS 查询，FLASH 参考在每次调用内使用固定温度
burner 并关闭面 EOS。用时比衡量这些配置任务的成本，而非等误差效率。

CPU 测量使用 i7-10700 的八个物理核，ARCH OpenMP8 与 FLASH MPI8 使用相同核心。
每项数值是三次完整进程的中位数，包含初始化和输出。CUDA 使用 RTX 3060 Ti 和
一个主机线程。CPU8、CPU16 分别测量，GPU 比值采用各案例中较快的 CPU 中位数。
正式计时时没有并行编译、性能探针或其他模拟。

## CPU 成本

下表对应 ARCH 源码 `51f982ab` 和[测量目录](results/o6-controls-20260927/final/)
所标识的程序。FLASH 重复测量存在初始化波动，因此同时列出原参考与重复测量。

| 模型 | ARCH 默认 s | 重复测量 FLASH s | 原 FLASH 参考 s | ARCH／原参考 |
| --- | ---: | ---: | ---: | ---: |
| Sod | 5.028 | 2.822 | 2.630 | 1.91 |
| Jeans 64² | 0.717 | 1.318 | 1.324 | 0.54 |
| Jeans 128² | 5.780 | 2.973 | 2.876 | 2.01 |
| Cellular A | 7.082 | 8.178 | 5.018 | 1.41 |
| Cellular B | 7.182 | 10.273 | 5.203 | 1.38 |
| Cellular 无燃烧 | 4.527 | 9.023 | 4.177 | 1.08 |
| Cellular 0.25 cm | 32.350 | 9.596 | 9.261 | 3.49 |


原成本目标为不超过 FLASH 原参考的两倍。五个案例达到目标；Jeans 128² 约超出
0.48%，0.25 cm Cellular 用时为参考的 3.49 倍，这两项仍高于该目标。

显式选择 Davis 波速后，细网格 Cellular 的 CPU8 中位数为 **24.176 s**，即参考的
**2.61 倍**。Davis 属于不同的数值方法，其与默认方法的比较预算为：质量／总能
积分相对差不超过 `1e-3`，密度／压强／温度体积加权 L1 不超过 1%，主要核素积分
差不超过 1%；同时检查共同终点、正性、组分归一和零修复。四个 Cellular 算例的
温度 L1 最大为 `4.89e-9`，各项检查通过。

细网格案例中，ARCH 和 FLASH 分别执行 4,337,664 和 1,441,280 个叶单元宏步，
宏步数分别为 258 和 103。约 3.01 倍的工作计数差有助于理解成本，但两者每个单元
宏步内包含的 EOS 和反应工作不同。

## CPU/CUDA 成本

| 模型 | 较快 CPU（线程数）s | CUDA s | CPU／CUDA |
| --- | ---: | ---: | ---: |
| Cellular A | 7.082（8） | 14.677 | 0.483× |
| Cellular B | 7.182（8） | 15.164 | 0.474× |
| Cellular 无燃烧 | 4.527（8） | 9.335 | 0.485× |
| Jeans 128² | 5.780（8） | 7.810 | 0.740× |
| Cellular 0.25 cm | 31.051（16） | 57.290 | 0.542× |
| SNIa 二维 Cartesian AMR | 5.077（8） | 3.072 | 1.653× |
| SNIa 三维柱坐标 AMR | 37.178（16） | 45.308 | 0.821× |
| Cellular 0.25 cm，Davis | 24.176（8） | 44.558 | 0.543× |


表中每次 GPU 运行均通过实际后端、终点、拓扑和场检查。受测二维四模块 AMR
获得 **1.65×** 加速；其余默认案例为 **0.47–0.82×**，在这些规模下 CPU 更快。
单独的 CPU 回归与记录中的参考对照：七个成本案例的场逐位一致，两个耦合案例的
最大归一 Linf 约 `7.62e-16`。
耦合检查覆盖五步，包含活动燃烧、热扩散、自引力及 AMR 拓扑变化。

这些计时对应各自记录的源码和几何。二维柱坐标极平面的测量不代表当前原生轴对称
`(r,z)` 坐标图；相关能力见[原生坐标验证](../README.zh-CN.md#原生二维柱坐标)和
[平台支持](../../../docs/releases/PlatformSupport.zh-CN.md)。当前 Helmholtz
恒星模型提供热传导。

## 复现对照

使用 [run_comparison.py](run_comparison.py) 和本机构建的 CPU、CUDA 程序。
先生成新的 CPU 参考输出，再核对 GPU 字段：

```sh
python validation/gravity/flash/run_comparison.py \
  --routes arch --cases a b noburn jeans128 fine snia2d snia3d \
  --threads 8 --repeats 3 --prefix cpu8 --output output/comparison \
  --manifest output/comparison/cpu8.json
python validation/gravity/flash/run_comparison.py \
  --routes arch --cases a b noburn jeans128 fine snia2d snia3d \
  --threads 16 --repeats 3 --prefix cpu16 --output output/comparison \
  --manifest output/comparison/cpu16.json
python validation/gravity/flash/run_comparison.py \
  --routes cuda --cases a b noburn jeans128 fine snia2d snia3d \
  --repeats 3 --prefix cuda --output output/comparison \
  --manifest output/comparison/cuda.json
python validation/gravity/flash/run_comparison.py --report-only \
  --manifest output/comparison/cuda.json \
  --reference-manifest output/comparison/cpu8.json \
  --reference-manifest output/comparison/cpu16.json
```

默认程序路径为 `build-cpu/bin/ARCH`、`build-cuda/bin/ARCH`；其他路径通过
`--arch-cpu`、`--arch-cuda` 指定。按实际机器选择线程数及可选的
`--affinity`／`--cuda-affinity` 亲和性掩码。脚本使用 NumPy 和 h5py，每次运行使用
新的输出目录、前缀和清单。已记录清单中的原始路径属于产生数据的机器，新的运行
需要自己的场参考。FLASH 还需要预先准备的程序、匹配的 MPI 启动器；Cellular
另外需要 `--helm-binary`。

## 数据与数值控制

- [计时、场和工作量数据](results/o6-controls-20260927/final/)注明表格对应的源码、程序、输入及每次采样。
- [控制与导数检查](results/o6-controls-20260927/checks/)包含 CPU/CUDA 契约和设备插桩。
- [独立 CPU 测量](results/cpu-followup-20261003/README.md)分别记录 EOS 复用、表加载和多重网格调度的源码身份。
- [完整的版本对应研究](https://github.com/Shiro-Akane/ARCH/blob/v1.4.0/validation/gravity/flash/O6ControlsAcceptanceReport.zh-CN.md)保留编译器比较、被否决的近似、精确预算及其他采样。

被否决的面近似不是运行选项。生产实现使用完整面 EOS 查询与严格浮点编译。
`hll_wave_speed`、`eos_coulomb_mult` 和 `dt_max` 的用法见
[配置参考](../../../docs/Reference.zh-CN.md)。
