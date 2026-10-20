# RZ 速度 div/curl 分量接线

## Finding 和修复
显式 AxisymmetricRz view 虽已消费完整环体度量，VelocityDiagnostics 的
dim<=2 分支仍将 mom_v 解释为 v_phi，产生错误 curl。
将现有三维圆柱 curl 分支复用于显式 RZ；无 phi 轴时 ddz=0，
但 v_phi=mom_w/rho 继续保留。原有 legacy Grid、polar/spherical
分支及导数实现保持。共享 is_axisymmetric_rz 区分显式 view 与未迁移 Grid。

## 独立解析检查
轴正则场 vr=(a+b*z)*r，vz=c*r²+d*z，vphi=(w+q*z)*r：
div=2(a+b*z)+d，curl_r=-q*r，
curl_phi=(b-2c)*r，curl_z=2(w+q*z)。
参考直接来自柱坐标微分定义，不调用生产 evaluate 或 EOS。
27 个单元覆盖 r_left=0/1/4，负 z 及第一轴单元；使用既有
2e-12 算术检查门槛，不创建或放宽科学预算。
CPU curvilinear_metrics 与 amr_operation_plans 2/2 PASS，diff check PASS。
精确 dirty 编译输入及 test ELF SHA 见同名 Summary.json；
日志留 studio/.local/integration/rz-velocity-diagnostics-20261004。

## 仍未完成
本轮不是 RZ full field/AMR capability 或演化验收。
黏性方向标签不能简单切换：无 phi 导数仍有正交基底的方位连接，
其源项和稳定性、能量 work flux 必须同步独立验证。
运输、完整 conservative AMR/elliptic/gravity/IO/checkpoint、CUDA 仍待迁移。
未构建 ARCH、未运行 simulation、未 push/tag/main merge。
