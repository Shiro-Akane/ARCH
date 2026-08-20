# ARCH Verification 与 Validation

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

本目录是定量验证记录的统一入口。首批 CPU 基线覆盖均匀网格流体重构、RKL1/RKL2 组分扩散、常外部重力和 aprox13 单区燃烧。只有 V2 后端能使用同一批已提交输入运行后，才能填写 CUDA 行。

## 目录契约

每个模块只拥有一个子目录，其中包含 README、机器可读指标、`inputs/` 下的不可变基线参数和可选的 `figures/`。每份验证 `.par` 都必须标明所属记录，并与相应指标及验收结论同步修改。可复用教学和示例输入保留在 `simulation/`，运行时数据保留在 `EOS_toolkit/`。不要再建立平行验证树。

## 当前状态

| 区域 | CPU 结果 | CUDA 结果 | 记录 |
| --- | --- | --- | --- |
| 光滑流体重构 | PCM、MUSCL 和 PPM 通过 | 待完成 | [hydro](hydro/README.zh-CN.md) |
| RKL1/RKL2 组分扩散 | 均通过；RKL2 显示二阶空间收敛 | 待完成 | [diffusion](diffusion/README.zh-CN.md) |
| 外部重力 | RK2/RK3 常加速度更新通过 | 待完成 | [gravity](gravity/README.zh-CN.md) |
| aprox13 单区燃烧 | BD 和 ROS4 通过 BE_NR 比较 | 待完成 | [burn](burn/README.zh-CN.md) |
| AMR | 仅定性档案；定量范数待完成 | 待完成 | [AMR](amr/README.zh-CN.md) |
| Sod、Sedov、EOS、几何、NSE、重启 | 待完成 | 待完成 | 已规划 |

“待完成”是明确占位，不是后端一致性的证据。

## 误差约定

Verification 将实现与解析解、制造解或独立收敛参考比较。与实验或已发表物理数据的 validation 会单独标记。

对按单元体积加权的场误差，

\[
L_1(q)=\frac{\sum_i V_i\lvert q_i-q_i^{ref}\rvert}{\sum_i V_i},
\qquad
L_2(q)=\sqrt{\frac{\sum_i V_i(q_i-q_i^{ref})^2}{\sum_i V_i}}.
\]

分辨率 (N) 与 (2N) 间的观测阶数为 (p=\log_2(E_N/E_{2N}))。每条记录必须说明不同的范数或归一化方式。机器可读结果保留为 CSV；当公式、采样输出、命令和指标足以复现结论时，可以不提交数据处理脚本。

## 记录要求

每条完成记录必须说明：

1. 测试性质与模块、方程、维度和几何；
2. 不可变 `.par` 输入，以及外部 EOS/参考资源的 checksum；
3. 配置、构建和运行命令；
4. commit、编译器/flags、OpenMP 数、后端及相关硬件；
5. 参考来源和采样规则；
6. L1/L2 或模块专用残差和不变量；
7. 验收容差与明确通过/失败结论；
8. 保留的 CSV 指标及有用时的静态图；
9. 保护机制触发或已知实现限制。

AMR 比较必须将参考场和数值场放到已说明的公共网格，并使用物理单元体积。CPU/CUDA 比较必须使用相同算例源码、参数、参考和指标定义。

## 待完成矩阵

| 区域 | 计划参考 | 必需测量 |
| --- | --- | --- |
| Hydro/Riemann | 解析一维 Sod 解 | 密度、速度、压力、能量 L1/L2；特征位置；守恒 |
| 强激波 | Sedov 相似解 | 径向 profile L1/L2、激波半径、能量、对称性 |
| 流体时间积分 | 光滑半离散参考 | Euler、SSPRK2、SSPRK3 随时间步误差 |
| 几何 | cylindrical/spherical 制造解 | 体积加权 L1/L2 和源项平衡 |
| AMR | 均匀网格参考和界面穿越 | 公共网格 L1/L2、reflux 守恒、restriction/prolongation 一致性 |
| EOS | 可信理想/表格/Helmholtz 状态 | 往返、状态 L1/L2、单调性和定义域检查 |
| NSE | 独立平衡状态 | 组分/热力学 L1/L2 和平衡残差 |
| HDF5/restart | 不间断运行 | 状态、拓扑和元数据一致性 |
| CPU/CUDA | 上述相同记录 | 场/状态 L1/L2、不变量、配置和设备元数据 |

新记录使用 [CASE_TEMPLATE.zh-CN.md](CASE_TEMPLATE.zh-CN.md)。
