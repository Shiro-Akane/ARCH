# RZ 完整 Host 扩散组合层

## 实现
Host compute_fluxes、divergence、geometric sources、compute_diffusion_operator
与 adaptive_dt_diff 携带同一显式内部 GeometrySemantics；
默认 Existing 保持旧调用方。make_geometry_view(Grid,semantics) 验证 RZ 的
cylindrical 2D 合同；组合算子在写出 destination 前完成 chart 检查。
face spacing、basis rotation、volume/area、源项与 dt 都消费同一 RZ view。
不建立第二套扩散公式，不添加公开 Config 开关，不切换 runtime Grid。

## 完整执行证据
真实 padded Grid / FluidState / IdealGas，原已有 nu=.03 测试材料。
r_left=0/1 两个 16×16 活动网格、全部512单元：
rho=2、vr=r、vz=3z、vphi=2r 对应 Cartesian 线性速度场；
完整 Host operator momentum=0、work=19mu、mass/species=0。
独立 dt = 1/[2nu/dr²+2nu/dz²+nu*<1/r>/r_first] 与实际最小归约一致。
所有 ghost 由解析夹具填充；不能冒充真实边界/AMR 同步演化证据。

变密度 rho=2+alpha*r，alpha=.1；mu=nu*rho。
独立 Cartesian 导数给 momentum=(nu*alpha,0,2nu*alpha)；
原生环体 volume-average work=nu*(38+24alpha*<r>)。
在固定 r_center=.5、h=.05/.025/.0125 下，
work 绝对误差依次1.125e-5、2.8125e-6、7.03125e-7，二阶。
采用既有 viscous 工程二阶/解析检查门槛，不声明新增科学验收预算。

CPU curvilinear_metrics 与 amr_operation_plans 2/2 PASS。
补变密度夹具后只重跑变化的 metric 测试，旧 AMR 无新改动不重复。
精确 baseline/dirty source hashes/test ELF SHA 见 Summary；
完整日志留 studio/.local/integration/rz-host-diffusion-20261004。
diff check PASS，未独立 configure 或完整 ARCH build。

## 未完成
真实 RZ Hydro/axis boundary/AMR conservative exchange/reflux、RKL 时间演化稳定性、
thermal/species 独立材料参考、elliptic/gravity/IO/checkpoint 和 runtime source identity
仍待成套迁移；CUDA 按计划在完整 CPU 阶段后执行。
本轮不运行 simulation，不 push/tag/main merge，不开放 RZ capability。
