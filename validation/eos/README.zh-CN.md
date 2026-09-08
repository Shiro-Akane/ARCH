# EOS 验证

英文原文：[README.md](README.md)。英文版为规范文本。

状态方程（EOS）把密度、温度和组分与压力、能量及其导数联系起来。测试既检查
这些局部热力学关系，也检查它们在流体和燃烧演化中的使用。各项参考对应文中
明确给出的热力学范围和表格表示方式。

本页结果对应[Validation 总索引](../README.zh-CN.md)注明的科学验收快照；源码组织与
构建检查见单独的[维护记录](../backend/results/maintenance-freeze-20260908/)。

EOS 验证结合独立热力学参考与实际流体、燃烧应用。Ideal、Helmholtz 和
规范化 Tabular3D/Tabular4D 在 CPU 与 CUDA 上共用数学实现；后端负责表格
存储及其生命周期管理。

## 耦合应用结果

[Release 应用记录](results/application-native-20260907/release-891/evidence.json)
通过了十二个案例、96 次 CPU/CUDA 执行。
[独立端点记录](results/application-native-20260907/endpoints-892/evidence.json)
检查了全部 24 个物理时刻端点。两份记录使用相同的源码、程序、比较工具和依赖库，
并检查它们在执行期间保持不变。

两种表格维数都使用解析理想气体数据，分别构建直接物理量表与自由能表。
四个案例在混合层级的自适应网格上演化熵波至 `t=1e-9`，将密度与精确有限体积
波形平均值比较。八个案例在固定密度下演化 aprox13 燃烧至 `t=1e-10`：
自由能表覆盖 BE_NR、BD 和 ROS4，直接物理量表使用 BD；这些燃烧案例关闭 NSE。
中间步检查补充固定物理时刻的解对比。

| 检查 | 已记录的最大误差 | 验收预算 |
| --- | ---: | --- |
| 解析压力，自由能表 | 相对误差 `2.975e-10` | `1e-3` |
| 解析压力，直接物理量表 | 相对误差 `3.236e-4` | `1e-3` |
| 熵波密度 | L1 / 平均密度 `9.190e-5` | `1e-3` |
| 流体质量、动量和能量守恒 | 相对误差 `2.065e-15` | `rtol=2e-12`、`atol=2e-11` |
| 燃烧源项能量闭合 | 相对误差 `3.713e-15` | `1e-12` |

CPU/CUDA 场通过 `rtol=2e-8`、`atol=1e-12` 的对比；燃烧质量分数满足 `1e-12`
的归一化预算。流体守恒使用物理单元体积。独立燃烧检查从原始质量分数计算核结合能
释放，并与守恒能量的变化比较；期望能量不调用生产 EOS 或时间积分器取得。

这些结果的覆盖范围由受测表格与轨迹确定。用户自备 EOS 表的有效性遵循其声明的
热力学定义域与数据契约。[验证索引](../README.zh-CN.md)统一记录整体验收状态，
汇总这些结果及完整回归、重启、设备安全检查、持续运行和容量证据。

## 独立数学检查

- EOS 测试比较 Host/Device 数值，并覆盖表格存储管理与错误路径。
  人工构造的多项式检查插值、组分梯度、Hessian 作用量与比热导数。
- Helmholtz 状态、温度反演和短等熵路径使用独立参考。弱／强库仑与辐射主导状态
  另行检查压力、能量、比热和电子量。
- [helm_reference.py](helm_reference.py) 根据独立端点约束，以两档高精度重建
  表格插值；它使用原始表数据和注明的常数，不调用生产 EOS 代码。
  声速与压力导数通过独立热力学关系核对。

## 复现

运行[应用脚本](results/application-native-20260907/replay.py)，指定 `--build-dir`
和新的 `--output-dir`。它生成验证表格，再通过共用应用验证器运行两个后端。
随后运行[端点检查器](results/application-native-20260907/check_terminal.py)，使用
相同的 `--build-dir`，以 `--report` 指向应用的 `evidence.json`，并选择另一个新的
`--output-dir`。这些脚本需要 NumPy 和 h5py，报告保留输入、科学预算及程序身份。

按[构建指南](../../README.zh-CN.md) 配置启用 CUDA 的构建目录。
并行数应按可用内存选择。数学测试也可单独执行：

```bash
cmake --build build --target arch_cuda_eos_host_device_parity
ctest --test-dir build -R '^eos_host_device_parity$' --output-on-failure
python validation/eos/helm_reference.py
```

独立脚本需要 NumPy、SciPy 和 mpmath，只输出参考计算，不自动更新测试数据。
规范化 CPU 表格回归也可单独运行：

```bash
cmake --build build --target tabular_eos_regression
ctest --test-dir build -R '^tabular_eos_ideal_gas$' --output-on-failure
```

## 表格式与后续工作

生产表格 EOS 输入遵循规范化 3D/4D HDF5
[数据契约](../../src/physics/eos/TabularEOS.zh-CN.md)。原生 Shen EOS4/EOSDriver
数据转换属于独立的后续扩展，需要明确单位、能量零点、热力学组成和有效范围。
[历史来源表评估](results/tabular-assessment-archive.zh-CN.md)保留了原始间距试验
与源数据分析。
