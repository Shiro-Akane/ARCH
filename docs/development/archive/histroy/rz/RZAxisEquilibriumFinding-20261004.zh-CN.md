# RZ-AXIS-01：刚体旋转平衡 initial residual 审计

基线97e3c41a，依据Core86bec324第8.1/8.4节。
这是公开失败/覆盖边界的review节点，不修改生产数学、不解除RZ gate。
物理门槛>=1.8不变；不挑选一个有利范数假装完整签收。

## 独立输入与实际测量

rho=Omega=1，gamma=1.4，P0=5，
P(r)=P0+rho*Omega²*r²/2，径/轴向速度0。
域r=[0,1]（含轴）或[1,2]（离轴），z=[-.125,.125]；
16/32/64/128径向cells，root blocks=1/2/4/8。
原生cell P、E是解析volume平均：
<r²>_V=(rhi²+rlo²)/2，
Pavg=P0+rho*Omega²*(rhi²+rlo²)/4；
E=Pavg/(gamma-1)+rho*Omega²*(rhi²+rlo²)/4。
m_phi=rho*Omega*W-centroid；ghost按解析延拓/轴奇偶值提供。
没有为了平衡而把true averaged KE改成代表KE，或补热/改E/J。

实际HLLC/MUSCL-MC和当前RZ source取一次initial derivative（dt=1仅测导数），
精确equilibrium radial rhs=0。没有simulation timestep演化或H5输出。
独立native V/W积分检查J与解析pi*rho*Omega*dz*(rhi⁴-rlo⁴)/2；
最大初态J相对误差约3.04e-17。
closure用真实EOS+显式gas composition和独立Pavg比较，
nonfinite明确失败。
测量norm为native-V weighted L1/RMS、全域Linf；同时输出峰值位置和EOS误差。

## 已确认结果，不扩大声明

离轴最高细化：pL1=2.00001、pRMS=2、pLinf=1.99441；
含轴：pL1=1.99949、pRMS=1.94197、pLinf=0.999997。
EOS representative压力误差两域p=2，
128cells max absolute约1.01725e-6。
含轴actual radial residual Linf：
0.0734159、0.0367092、0.0183548、0.00917741；
峰值始终首个axis邻格（r=dr/2）。
加权范数满足1.8，但首格局部force仅一阶；总J守恒不证明局部force正确。

目前§8没有冻结唯一norm及独立axis-local允许的具体预算。
本诊断保守要求全部列出的norm达1.8，否则exit2/NOT_CLEARED；
这不是擅自宣布Core所有空间验收失败，也不是新增永久production标准。
需Core明确最终norm与axis-local指标，不能静默删掉Linf列来称PASS。
仍需要evolved smooth equilibrium/convergence与strong-swirl检查，
本t=0 initial derivative不替代它们。

## 可定位的数学差异与职责

独立解析face pressure divergence与真实cell source比较，首格[0,h]：
W-centroid=3h/4，m_phi²=9h²/16，
true KEavg=h²/4，representative KE=9h²/32；
EOS压力=P0+19h²/80（gamma-1=.4）。
当前piecewise-constant source=(m_phi²+Pclosure)*2/h
=2P0/h+8h/5。
解析pressure face divergence=2P0/h+h；
其差=3h/5=0.6dr，已实际逐分辨率验证：
.0375/.01875/.009375/.0046875。
即使face pressure精确，source闭合也存在轴局部O(h)项；
actual MUSCL/mean-to-face重构还贡献另一部分残差，
未把source-only差说成全部最终误差。

职责：
GridMetrics继续单一native V/W/centroid/face moment owner；
Reconstruction/FluxTraversal应识别不同权平均到物理面的几何适配；
GeometricSources需同一物理P/rho/u_phi的矩一致source求积；
EOS维持唯一共享物理定义，不引入第二热模型；
Init/BC/C接口与state语义仍按后续消费者计划统一。
候选修复是几何感知weighted reconstruction/source quadrature，
并以现有科学式验证，而非从Omega猜模型、hardcode equilibrium correction、
floor、softening、补热或改变能量/角动量定义。
需要对一般smooth/strong-axis场同样成立，不能只拟合本fixture。
RZ-VISC-01独立保留；其他ring/C消费者依赖可继续。

## 工具问题与真实finding分离

首次测试类型MC实际应为McLimiter，编译拒绝后修正。
首次EOS诊断用nullptr composition（gas count1）得到nonfinite，
std::max掩盖为closure0。已改显式X=1+finite检查，
旧错误测量不用于科学结论，原日志保留。
正确重测仍确认上述axis force一阶；不是composition问题导致的fake finding。
未修改production算法、物理阈值或输入以隐藏失败。

## 复现与交付

cmake --build build-cpu --target arch_curvilinear_metrics --parallel 28
OMP_NUM_THREADS=1 build-cpu/arch_curvilinear_metrics rz-equilibrium-audit
预期当前exit2，RZ_EQUILIBRIUM_SPATIAL_GATE=NOT_CLEARED。
该explicit科学诊断与默认compatibility suite分开报告：
curvilinear_metrics/amr_flux_surface_plan 2/2、architecture、diff check PASS；
这些green checks不覆盖/不关闭本科学finding。

只重编译test executable。production ARCH仍
1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020，
不重复同binary的JENS9+9或其他已完成基线。
摘要：validation/amr/results/rz-equilibrium-audit-20261004/summary.json。
原日志：studio/.local/integration/rz-equilibrium-audit-20261004，本机保留。
未上传原始H5/plt/checkpoint，未开展CUDA/长跑/Windows。

Core review问题：最终空间norm、axis-local强检查与允许误差域如何冻结？
上述geometry-aware source/reconstruction候选是否符合预期范围？
在明确结果之前，不把加权global PASS表述为axis完整科学签收。
