
# RZ 轴邻格最小决策包
状态：待 Core 确认。Finding RZ-AXIS-01 未关闭。
历史证据 RZAxisEquilibriumFinding-20261004.zh-CN.md。

## 当前证据
rho=Omega=1，gamma=1.4，P0=5，P=P0+rho*Omega²*r²/2。
r=[0,1] 或 [1,2]，z=[-0.125,0.125]，N=16/32/64/128。
真实 HLLC/MUSCL-MC 初始 RHS（dt=1 只求导，不作演化）：
离轴 L1/RMS/Linf 约二阶；含轴 L1约1.9995、RMS约1.94197、Linf约1。
不能仅用全域守恒或 L1 代替首轴邻格的局部正确性。

## 候选与独立参考
沿原 weighted reconstruction / source quadrature 所有者修复一般几何映射；
不加只针对 Omega 均衡态的补丁，不用代表态动能替换真实积分。
首格 [0,h]：真实 KE=h²/4，代表态 KE=9h²/32；
当前 EOS P=P0+19h²/80，当前 source=2P0/h+8h/5，
解析 pressure divergence=2P0/h+h，source-only 差为3h/5。
以独立符号积分/Fraction 构造 native V/W、压力面项、几何源和 EOS 转换，
局部量分别归因，不能只比较最终相消后的全域量。

## 验收候选与待确认
原制造解 >=1.8 收敛要求不放宽；同时保留全域 L1/RMS/Linf、
首邻格及固定物理邻域的 force/EOS/energy 局部指标。
Core 冻结局部采样域、归一化、误差界及时间演化适用范围；
必须覆盖轴线/离轴、非均匀格、混合 AMR、真实 ghost 与 regrid/restart。
实施方可准备独立参考和候选修复，确认前不以新 norm 排除坏格。
本项关闭也不替代连续面力参考、粘性与外源签收。
