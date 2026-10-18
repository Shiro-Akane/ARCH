# RZ composite Poisson 与 multigrid 制造解

## 接线
EllipticMesh 携带与 GeometryView 相同的显式内部 semantics，默认 Existing。
validate_mesh 限制 RZ 为 cylindrical 2D，取消该模式的 x2 full-turn 要求，
仍核对半径、spacing、逻辑大小。CompositePoisson leaf volume、face area
消费共享 full-ring 度量；两向 native/physical scale 都为 1。
r=0 为零面积正则面，非零内边界为 Dirichlet；两端 z 为物理 Dirichlet，
不再连接 periodic donor。沿用既有二次 face-fit/flux owner、volume norm 与 MG。
MG 粗层复制同一 mesh 身份，原有 child/parent volume 检查通过。
生产 Grid/API 装载器不切换，未开放 RZ capability。

## 制造解与真实指标
计划指定 Phi=a*r²+b*z²；本次 a=.75、b=1.25，
A=-L，RHS=-(4a+2b)=-5.5，face gradient=(1.5r,2.5z)。
潜势为 cell-center 点值，不冒充 cell-average oracle。
解析边界仅用于算子试验，没有调用或验证生产 finite-ring 外边界。

四组：inner=0/.5，uniform/mixed；64/112 cells，混合每组32个 coarse-fine faces。
算子重现最大绝对误差 2.2737367544323206e-13；
精确 Phi 数组的 face gradient 最大误差 2.2204460492503131e-15。
求解后潜势最大误差 1.0710876630071198e-11；
求解后 face gradient 最大误差 1.3740331095135616e-10。
所有 residual <= 对应请求 target；完整逐组值、cycles、volume 见 Summary。
求解误差与 stencil 重现误差分别记录，不能以残差替代力/势的科学验收。

新的 polynomial gate 仅为本次算术工程检查，不声明 Core 已冻结新的
RZ 科学误差预算。既有 contract/radial、curved、singular 回归使用原门槛通过。
最后补充 solved-face-error 输出后只重跑受影响 RZ witness；未重复旧回归。
精确 baseline/dirty inputs/test ELF SHA 见 Summary，日志留
studio/.local/integration/rz-composite-poisson-20261004。

## 未关闭的出口
仍缺环体近远场方案和独立预算的 Core review，density/mass-moment
production boundary 接线，实际 mixed AMR 演化收支、IO/checkpoint、
完整 runtime identity 与受影响 CUDA。解析边界不是生产孤立引力通过。
本轮只编译限定 test，不构建 ARCH、不 simulation、不 push/tag/main merge。
