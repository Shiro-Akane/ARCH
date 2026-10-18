
# RZ variable-mu 粘性应力与能量功决策包
状态：待 Core 确认。Finding RZ-VISC-01 保留。
证据来源 RZViscousStressReview-20261004.zh-CN.md；本包不是新运行证据。

## 现有行为与冲突
当前角向 face flux=-mu*v_phi'，几何源=-mu*v_phi/r²；
L_old=(1/r)(r mu v')'-mu v/r²。
单纯改为 torque divergence (1/r²)(r² mu v')' 也不等于对称应力。
若采用 tau_rphi=mu*(v'-v/r)，则 L_shear=L_old-mu'*v/r。
刚体旋转 v=Omega*r 的物理对称剪切为零，而旧 variable-mu 算子留下 Omega*mu'。
常 mu 的通过不能关闭 variable-mu finding。

## 候选定义（需签收，尚未修改算子）
在既有共享 diffusion owner 贯通对称应力、原生力臂/面积和配对能量功，
CPU/device 使用同一数学定义，不建立另一套粘性状态。
tau·v 面功与应力散度必须配对，账本同时核对动量、总能量和热耗散；
不以单独总能量守恒掩盖错误应力，不加数值 heating/floor。

## 独立参考/指标
独立解析展开 constant/linear/quadratic mu 与刚体/非刚体 v(r)；
轴外先核对逐点应力、散度及 div(tau·v)，再做 native face/volume 离散收敛。
增加 mu'!=0、刚体应力应为零和符号/几何源反例；保留质量、rhoX、repair=0。
全域 norm、边界功和局部应力残差分别报告。
原预算不改变；新 variable-mu 能量参考、域与容许误差由 Core 冻结，
不能依据候选观察误差制定阈值。

## 待确认
采用的物理应力张量、mu 是动力还是运动粘度、能量功/耗散符号；
轴处正则条件、强旋流适用域、时间/空间 norm 及门槛。
确认后才实施生产改动及科学验收，不把本草案计为通过。
