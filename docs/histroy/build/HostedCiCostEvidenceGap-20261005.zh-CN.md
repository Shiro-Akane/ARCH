# CI hosted cost：历史证据与当前缺口（2026-10-05）

按联合计划6.2只读查询真实GitHub Actions，基线edda3b7cc283977960e3311aaf88fb6758f3ec53。
没有启动CI、建立workflow、改变触发/timeout或重跑科学短包。

## 实际证据
两个成功ARCH CI，runner labels均ubuntu-24.04：
- 36329736632，compute/optim，131118ccc3a0167656dd1b6834997341c65b16b0。
  Tooling43s、CPU Release527s、required3s；CPU compile333s、CTest134s。
- 36330347084，main，25adec4224497981a0c124a3485f786194975be4。
  Tooling37s、CPU Release497s、required4s；CPU compile289s、CTest126s。
- job/step时间来自公开 /repos/Shiro-Akane/ARCH/actions/runs/{id}/jobs。
  这是实际历史耗时；whole-workflow排队不能等同最长单job。

旧workflow SHA8fbd89eb7a9dd9fd880ad50b1bfa109fe9ca692f2db8fbdbab52e342fe331e44，
当前35cec3d3a10f05c25c081dbb1000ee98a498a2cf56b3c57f12242fcacd4cd492。
两旧入口仅Tooling/CPU Release/CI required，没有当前Studio/Host。
API按studio/compute-optim-integration过滤返回total_count=0。

## 判定与下一步
CI-HOSTED-CURRENT-01 OPEN；不能用旧green证明当前配置/Preview/JENS/Studio。
冷/缓存比较未取得：restore步骤success不证明hit，后次更快不证明cached；
同runner label也不证明硬件相同。不把当前分支未触发CI写成科学FAIL。
沿既有main-oriented PR入口取得当前候选正常运行，保留准确SHA、
runner/cache hit/miss/ccache和各组/关键路径耗时，再作可比结论。
本轮没有修改触发策略或发起运行。

处理summary：validation/tooling/results/hosted-ci-cost-audit-20261005/summary.json。
所用job/step字段仅本机studio/.local/integration/hosted-ci-cost-audit-20261005/。
未取原始科学输出/数组，root STATUS不变，原科学门槛和JENS CUDA待授权事项保留。
