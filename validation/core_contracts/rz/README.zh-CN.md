# Core RZ 独立解析参考

契约：`CORE-RZ-20261006-v1`。完整分工、定义和运行范围见 [Core 确认](../../../docs/development/CoreRZDecisions-20261006.zh-CN.md)。此处只有解析 fixture 和静态输入核对，新增内容从 main 基线交付，不包含 compute 分支生产算子。

在仓库根目录执行：

```bash
python3 -m unittest discover -s validation/core_contracts/rz -p 'test_*.py' -v
python3 validation/core_contracts/rz/analytic_reference.py
```

只需 Python 标准库，不配置 CMake、不编译 CUDA、不调用真实 solver。

| 参考 | 独立检查内容 | 限制 |
| --- | --- | --- |
| 外源 | 非均匀 m_phi 的 W 力矩与 V 功；含轴正则源；拒绝含轴非零常数 g_phi | rho 常数、物理阶段场为多项式；不是任意 case 的重构器 |
| 方位粘性 | constant/linear/quadratic μ，刚体与 Ωr+ar³；应力/力/功/热耗散恒等式 | 径向-only 方位 fixture；线性 μ 限离轴，非全应力张量 |
| 近轴平衡 | native V/W、真实/代表 KE、EOS 差、压力/几何源；保留旧 source-only 一阶反例 | 独立初始 RHS 参考，不是时间演化/AMR/ghost 验证 |
| benchmark 静态核对 | 四份输入及原输入、声明覆盖值、network SHA和表 payload 身份 | 不调用 Setup/EOS，不代表新终点科学通过 |

JSON 的 Fraction 以有理数字符串输出。所有带 `per_2pi` 的积分量采用 `dV/(2*pi)=r*dr*dz`；完整物理总量须统一乘 `2*pi`。V、W、J、功都用同一方位因子，均值不需要再次乘它。不得把这些数字直接标为已乘完整方位角的 CGS 总量。

压力及 energy 单元平均用 V，方位动量用 W；一般重构必须分别保证这些平均，不可用保存的 W 平均替代 V 的功积分。工具提供给生产 test 的期望值，但不导入共享 production 数学作为独立 oracle。

完整连续 Newton 面力、源内/接触误差界及真实演化预算不在这个工具里。它的测试通过只记 `ANALYTIC_FIXTURE_REFERENCES_ONLY`；四项 RZ finding 和 CUDA JENS public gate 仍各自保留。

输入单位：rho 为 g/cm³，r/z 为 cm，dt 为 s，Ω 为 s⁻¹，a 为 cm⁻² s⁻¹，μ 为 g/(cm*s)，P0 为 dyn/cm²。V 总量为 cm³，W 为 cm⁴，J 为 g cm²/s，能量总量为 erg。μ 多项式是参考工具的输入，不新增生产配置参数。
