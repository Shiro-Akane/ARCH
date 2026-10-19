# 成对 runner：显式物理终点模式

基线5a44cbec69d92e87e60427ea692f8c08b6448caa；本轮只有执行／后处理工具和合成测试。
没有运行真实 ARCH、CPU/CUDA 性能轨迹或改变科学 Core。

## 行为与兼容

既有 --pair label:input.par:steps[:amr|regular] 保持固定步数模式。
新增互斥 --endpoint-pair label:input.par:T_END[:amr|regular]；
T_END 必须来自 owner 冻结计划，不由工具选择推荐值。
生成受管输入仅改变 backend、out_dir、max_steps=-1、tmax=T_END；
源输入的 CFL/物理/EOS/阈值等不变，生成输入记录 SHA。

每个 backend 成功退出后立即核验终点，未到终点拒绝，不继续下一 backend。
final H5 time 使用既有 coupled 预算 max(1e-20,2e-10*scale)，没有调整门槛。
记录真实完成步数并检查全部 step rows；允许 CPU/CUDA 自适应步数不同。
同终点仍保留原四模块、字段/AMR topology、修复、重力残差与 parity 检查。
现有 strict initial+final 两份 plot 合同继续要求；多输出场景不是本轮支持范围。
summary 添加 requested_endpoint、input_sha256 与逐后端 endpoint_verification。

## 验证

独立 ignored Python3.12 venv：numpy2.5.3、h5py3.16.0。
validation/gravity/curved/test_physical_endpoint.py -v：8/8 PASS。
CLI --help 与 git diff --check PASS。

覆盖：
- endpoint 输入去掉 step cap，固定步模式仍保留原 tmax；
- 非有限/非正终点与矛盾 step+endpoint 请求拒绝；
- synthetic CPU2步/CUDA3步，同终点允许，旧 fixed2步仍拒绝CUDA3步；
- exit success 但未到 endpoint 拒绝；
- one_run stub 早停立即拒绝、run.log 保留；
- repairs/Poisson missed target、缺失rows/零步仍拒绝；
- 原 normalized Linf 预算超限仍拒绝。

测试只生成小型 synthetic H5/logs，不启动 binary；不是科学结果。
首次系统 Python 缺 h5py、文本编辑换行与遗漏 import 错误均保留本机日志。
修正后只重跑受影响 suite，未通过删除／放宽测试绕过。

## 保留出口

这只是正式测量所需停止模式，不是完整 benchmark protocol。
预热、至少三次交替成对顺序、实际线程／亲和性筛选、CUDA Host预算、
CPU-only 对照、来源／有效输入／设备 manifest、采样与失败汇总仍需补齐。
当前 CUDA Host1线程与旧默认值不得直接当成本机正式冻结设置。
端点、独立参考、误差预算仍需 owner 冻结；历史非物理 G 样本未批准换算。
继续按 CPU→CUDA 顺序验收，不以本轮 synthetic tests 宣称科学或性能通过。

原始测试日志和独立环境留在
studio/.local/integration/physical-endpoint-runner-20261003；不提交 raw H5/数组。
无 simulation/生产Build/push/tag/main merge。
