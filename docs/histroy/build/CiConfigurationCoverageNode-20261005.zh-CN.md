# CI 配置 / Preview / JENS 覆盖锚点节点（2026-10-05）

## 结果与实际缺口

处于联合计划第6节CI覆盖/耗时收束。基线4263b0583cd8c505966e5b31118cc978466abb44。
实施/工程验证PASS；不是hosted CI、科学或性能签收。

读取当前真实CPU inventory，71项。旧check_ci_results.py只要求18个锚点，
configuration_v3_contract等迁移后契约未列必需。
实际从此inventory移除configuration_v3_contract，旧checker接受剩余70项；
这说明“所有已注册测试都通过”无法发现关键注册被误删，不是科学fixture测试失败。
原始inventory及before结果保留本机studio/.local/integration/ci-contract-anchor-20261005。

## 最小改动

复用原tools/check_ci_results.py和tests/tooling/build_tools/test_ci_results.py；
增加7项parser/resolution/direct-entry/v3/API、12项已有Preview契约、
2项JENS诊断/indicator数学，CPU锚点合计39。
不冻结总测试数：当前71只是该build tree观测值，额外测试仍允许并要求全部JUnit记录。

独立测试显式列出21个要求名称；逐个删注册都必须拒绝。
旧missing/extra/duplicate/skip/nonzero计数、driver-cuda、Node TAP反例继续通过。
不把RZ未签收的显式诊断塞进默认科学PASS套件，不用空测试/skip绕过gate。

## 验证及成本

checker suite 13项PASS，21项删除subtest控制PASS；
实际71项inventory通过新锚点检查；缺v3反例改为
cpu coverage is missing: configuration_v3_contract。
本机checker/读inventory检查约0.068秒（诊断范围，非平台benchmark或hosted runner预算）。

ci.yml未改；现有tooling unittest自动发现新增控制，不增加job或平行新配置CI。
原CPU完整inventory执行次数仍1；新增Core执行次数0。
Studio仍一次Node suite含Host，build含tsc；不重复test:host或typecheck。
原main-oriented PR触发、required汇总、timeout/并行/cache/security/上传边界全部保持。
本次未重建ARCH、未运行科学suite、无H5/checkpoint输出。

CI冷/缓存build及组关键路径需实际同类GitHub runner结果，不从WSL时间推测。
当前活动分支不触发现有main-oriented CI不算失败；review/合并后的真实结果仍待取得。
因此“CI覆盖锚点修复”完成，但完整CI实际运行/成本收束不能全勾选。

## 复现

    python3 -m unittest discover -s tests/tooling/build_tools -p test_ci_results.py -v
    ctest --test-dir build-cpu --show-only=json-v1 > LOCAL_INVENTORY.json
    python3 tools/check_ci_results.py --inventory LOCAL_INVENTORY.json

独立证据summary在validation/tooling/results/ci-contract-anchor-20261005/summary.json。
未改变生产物理、科学预算、CUDA/JENS公共入口或RZ gate；后续科学短包/长包门槛分别保持。
