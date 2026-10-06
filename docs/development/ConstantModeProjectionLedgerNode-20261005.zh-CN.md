# 原生常数模态投影误差账本节点（2026-10-05）

## 结论与范围

SCOPED PASS；FULL PHYSICAL SOURCE CHAIN PENDING。production RZ gate 保留。

CompositePoisson 所有者新增 companion method，核对实际已计算 projection 数组
相对 P_w*x = x - sum(stored_weight_i*x_i) 的误差。非 periodic 域按既有语义为
identity；不擅自减均值。本节点不改变生产 mean/project/solve/source 算法。

只读确认生产 SelfGravity 的线性组合减均值与 UniformGravity 的先相减后乘法
顺序不同；二者之后都有 constant-mode projection。因此本节点只关闭 projection
算术的一个组成项，不能把它当成全部 physical source 构造已证实。

## 实现

复用实际 CompositePoisson 中 RHS/residual companion ledger 的 outward basic
arithmetic，数学加权和与相减各自围住，再逐 cell 围住实际 output 的距离。
不假定 CompensatedSum/provider reduction 或实际减法完全精确。
norm_upper 沿实际存储的 native norm weights；不改成体积重算的第二权威。

精确零输入/output 的 cell/norm bound 保持零，无人为误差 floor。
input/output extent 与 finite 状态验证；不能表示有限可靠区间时沿现有
Overflow 传播。错误的 output 不冒充精确投影，而获得相应非零误差界。
不授予完整 physical quality 或 source publication token。

P_w 的 stored weights 在本次 Cartesian fixtures 中精确总和为 1，但生产代码
不普遍假设任何几何域权重之和精确为 1。本方法未覆盖 weights 构造、理想 native
volume、periodic compatibility 或 mean source 输入的物理真实性。

## 独立证据

20 lanes：Cartesian uniform/mixed（16/28 cells）乘 periodic/Dirichlet，分别为
zero、constant 1e7、1e12 背景加微小对比、正负 1e100 大值抵消、次正规值。
实际调用 op.project；独立 Python Fraction.from_float 计算 exact stored expression。
逐 cell error bound 与 exact weighted norm 平方均通过，不调用 producer 函数。

零输入维持零预算；wrong output、NaN input、缺失 input 的反例通过。
边界：constant 输入的实际投影即使为零，通用区间仍可能给保守非零上界。
大背景微小对比的上界可能大于实际 signal；不得据此宣称原 1e-10 科学
容差已通过，不把当前 companion 界代替独立科学参考。

Core targeted CTest 四项 PASS：
gravity_stage_contract、composite_poisson_analytic、
self_gravity_lifecycle、composite_poisson_contract。
boundary-acceptance 与原 18 lanes RHS/residual Fraction reference PASS。
tools/audit_architecture.py 与 git diff --check PASS。

第一次架构调用误用不存在的 tools/check_architecture.py，退出 2；完整错误
留本机 architecture.log。改用仓库真实入口后退出 0；未绕过有效检查。

## 构建、身份与留存

baseline 282da3bd5078a3662fbf185494847cc2048eabbe。
仅唯一 ARCH-compute-optim 既有 CPU Release build-cpu 的标准增量构建，
ARCH / scoped tests，parallel 28，memory guard 无触发、swap 增长 0；
peak owned RSS 2534184 KiB，min available 20760604 KiB。

实际 ARCH SHA256 构建前后相同：
d83186386a3dbcb403f473426da7dd42d1fc42840b09f7ddc43a7977fb36d95e。
新增函数经本次 scoped executable 验证；不重复已匹配 ELF 的
JENS 冻结 9 短演化＋9 checkpoint restart。未执行新的 simulation 或 CUDA。

复现：

    python3 validation/gravity/constant_mode_projection_reference.py --probe build-cpu/arch_composite_poisson --output <local-summary.json>

原生数组和完整日志留 studio/.local/integration/constant-mode-projection-20261005；
只提交源码、工具、处理后 scalar summary 和文档。

## 后续依赖

物理 density mean/source preprocessing 与 projection 的组合误差、native
geometry/stencil/weights 构造、实际 source/tree/AMR 身份和 whole original RHS
acceptance 仍需贯通。一般父节点 far certificate、RZ A→B→C→D 消费链、
RZ-VISC-01 / RZ-AXIS-01 未决科学参考保持。CPU 科学 gate 后才统一 CUDA；
不改变科学定义或阈值，不开展 Windows 适配。
