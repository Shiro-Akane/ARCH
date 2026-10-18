# RZ 黏性未解析方位连接

## 实现依据
现有黏性 owner 计算 div(mu grad(v)) 与其保守 work flux。
RZ 无 d_phi(v)，但圆柱正交基仍有 C_phi(v)。
复用既有 3D cylindrical 的 ViscousBasisRotation(0,-1/r,0)，
r/z 活动方向连接为零；未解析 phi 源为 C_phi(C_phi(v))。
因此径向/旋流局部源分别为 -mu*v_r/r*<1/r> 与
-mu*v_phi/r*<1/r>，z/mass/energy 不加源；不加载 phi 邻居。
共享稳定性叶同步加入 nu*<1/r>/r，保留实际 face transport 贡献。
默认 legacy polar/3D/Cartesian 路径不变，未切换 native Grid 装载器。

## 独立局部参考与 CPU 检查
Cartesian v=(x-2y,2x+y,3z)，对应 RZ vr=r、vz=3z、vphi=2r。
Cartesian 线性场 vector Laplacian 为零，work divergence 为
mu*sum_ij(d_i v_j)^2=19mu；参考不调用生产连接或通量。
共享 face flux + authoritative IdealGas geometric source 在
r_left=0/1/4 的局部 cell 上验证 momentum=0、work=19mu、
rho=0；单独检查 r/phi 源、源 row bound、z/mass/energy 无改动。
计数 StateReader 确认未解析 phi 没有 neighbour read。
用已有 2e-12 算术检查门槛，未设新科学预算。

限定 CPU arch_curvilinear_metrics 与 AMR 回归 2/2 PASS；
原 metric/CFL/viscous convergence/origin/stability 检查保持。
首轮新测试 make_diffusion_config_view namespace 遗漏，已修正；
初次和最终日志保存在 studio/.local/integration/rz-viscous-connection-20261004。
编译 dirty inputs 与 test ELF SHA 见同名 Summary.json。

## 仍需完成
局部线性算子证据不等于变系数 RZ 空间收敛、演化稳定性或科学签收。
Host native Grid 完整 transport/diagnostic 分派、AMR measures/reflux、
elliptic/gravity、IO/checkpoint identity 尚待成套迁移；随后再统一 CUDA。
本轮不构建 ARCH、不 simulation、不 push/tag/main merge，不开放 RZ capability。
