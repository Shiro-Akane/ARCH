# RZ 几何动量源项共享入口

## 实现与范围
为完整旋转体 r/z/phi 基底提供显式局部源项入口；mom_u=r、mom_v=z、
mom_w=phi。压力由调用方 authoritative EOS 读取一次；不增加 floor 或修改状态。
体积平均 1/r 为 (r_right-r_left)/annulus，2π 与 dz 抵消，轴单元保持有限。
共享圆柱公式为径向 (rho*v_phi²+p)<1/r>、phi 方向
-rho*v_r*v_phi<1/r>；不添加质量、轴向或总能量源项。
现有二维 Cylindrical 仍为 polar r/phi；其 dispatch 未迁移，未开放 RZ capability。

## CPU 证据
只构建 arch_curvilinear_metrics，ctest curvilinear_metrics 1/1 PASS，diff check PASS。
轴单元/普通单元压力面通量与源项平衡、非零旋流、轴向速度独立、
种子 delta 未相关分量保持，以及旧圆柱 1/2/3D 原公式逐项精确一致均通过。
原有 metric/CFL/viscous 检查同一测试通过；未放宽既有 2e-12 算术门槛。
初次测试编译失败：误用 FluidVector.energy；真实字段为 eng，已修正。
失败及最终日志保存在 studio/.local/integration/rz-geometric-source-20261004。

精确基线、修改输入 fingerprint 与 test ELF SHA 见同名 Summary.json；
二进制不是文档提交 HEAD 的 clean full ARCH build。

## 未完成部分
局部源项代数不证明演化角动量守恒。尚需完整 RZ hydro/diffusion/axis boundary、
AMR/elliptic/cache、IO/checkpoint 语义与 CPU 科学验收，之后再统一 CUDA。
O7.1 与有限环近场的 Core review gates 保持；本轮未执行 simulation。
