# RZ 孤立引力边界误用保护

## 实际 finding
新增内部 RZ EllipticMesh 身份后，原 GravityBoundary 只按 dimension/geometry
选择核：二维 cylindrical 自动走 isolated_log_potential，并以旧 polar
PhysicalPosition 构造源树。显式 RZ 输入因此会得到错误模型。
不能因为 RZ composite 算子/解析边界通过就允许这种生产混用。

## 修改和证据
GravityBoundary 构造及 values 两入口明确拒绝 AxisymmetricRz，
错误为 RZ isolated boundary unavailable: finite-ring contract pending。
双入口保护防止新建错误树，也防止缓存 legacy 树消费新 RZ operator。
旧 default/polar/3D 路径保持，未选择新的有限环体积分阶数/开角/预算。

限定 arch_composite_poisson CPU build；新构造拒绝、缓存 values 拒绝、
旧 polar finite values、RZ 解析制造解以及既有 boundary/curved 回归通过。
准确 binary/build-input 身份见同名 Summary.json；完整日志留
studio/.local/integration/rz-boundary-guard-20261004。diff check PASS。
本次 tests 确认拒绝原因包含 finite-ring contract pending，
不把其他异常误算为保护通过。

## 后续
Core 审阅有限 cell/ring 语义、近场/源内处理、远场矩/开角和独立预算后，
再在现有 tree/moment owner 实现真实环体路径；届时替换明确 Stop Gate，
同时测试误用拒绝与正常 path。不能偷偷 fallback 为 2D log 或整环单点质量。
目前不开放运行能力、不 simulation、不 CUDA、不 push/tag/main merge。
