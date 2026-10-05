# 重力请求身份：计算前检查

基线 1abf61913a9f72aad7176ef947122f2779728ae7；唯一 Linux/WSL 工作区。
finding GRAVITY-IDENTITY-PREFLIGHT-01 在 SelfGravity 请求入口范围关闭，
不代表完整 RZ Runtime 或科学能力签收。

## 复现与修复

原 prepare 在 gather 前只匹配部分绑定字段，没有消费共享
validate_gravity_solve_identity。NaN time 请求最终被 publication 拒绝，
但真实 GravityExecution 已经执行工作；新增负例先在原代码失败：
malformed identity executed gravity work before rejection。
原失败记录保留在本机 .local，不覆盖或改成 PASS。

修复是在原 invalidate 后、任何 gather/solve 前调用原共享校验，
没有另造身份规则、density 路径、求解器或科学阈值。即使非法请求，
旧 publication 也先退役。资源和数值数学不变。

## 实际验证

原 native 4-block Fixture、真实委托 Host executor 验证五类请求：
NaN/infinite time、非首块零 version、零 storage generation、非法 slot。
每次均有先前合法场；非法请求执行器工作计数增量为零，旧场不可读，
随后合法请求可恢复。这是入口/发布工程验证，不是时间演化。

CPU lifecycle 1/1 PASS，12.14 秒；CUDA-enabled Host 同一测试 1/1 PASS，
12.13 秒；后者不声称 Device JENS/RZ 数值签收。
重编实际 DriverRuntime 合同六源并链接当前 archive：
4→8→4、7 gather、非首块/slot/version 与 retired lease 检查 PASS，
time=0、steps=0。架构审计和 diff check PASS。

受影响 CPU target 增量 4.031 秒、owned RSS 峰 396296 KiB；
CUDA-enabled Host target 增量 6.025 秒、峰 504992 KiB；swap=0。
生产 CPU/CUDA ARCH ELF 未变化，旧科学 receipt 保留其编译身份，
不能作为当前 source/build Manifest freshness 证明。

[处理后摘要](../../validation/gravity/results/gravity-request-preflight-20261005/summary.json)
包含源码、测试 ELF 和生产 ELF 指纹。原始日志/ELF 留在
studio/.local/integration/gravity-request-preflight-20261005 及同 prefix logs，
actual Runtime 留 gravity-request-runtime-20261005。

## 后续

完整 RZ 全块 source→ring→native RHS/residual→solve→force/work→publication
仍未贯通；一般坐标误差、连续势/力、axis/viscosity、A→B→C→D 科学出口保留。
本节点不开放 RZ 或 CUDA JENS，不运行 simulation，不生成新 H5/checkpoint，
不启动长跑/计时，不做 Windows 适配。
