# 四个既有测试改动的 S0 差异审查

基线 `main@7d4448a9`；原四文件改动约 129 行增加/22 行删除（最终以 diff 为准）。这是测试能力扩展，不是生产性能优化。尚未通过修改后的大网络 CUDA 运行；不得宣称扩展容量已科学验收。

| 文件 | 动机 | 选择/退出/超时/容差/身份影响 |
|---|---|---|
| `cmake/tests/HostTests.cmake` | 注册 `sparse_validation_contract` | 仅新增 CTest 条目，不移除原测试；无新科学阈值；具体 inventory 按构建确认 |
| `tests/cuda/test_generated_sparse_burn.cpp` | 增加 `--storage-cells FIRST SECOND`、`--pool-cells COUNT` | 默认 2/3 和 pool=2 不变；非默认仍跑 BE_NR/BD/ROS4、全部步和字段；参数须正整数且容量合法/不溢出；非法参数退出 1。无超时修改，非默认增加 storage_controls 身份行 |
| `validation/network/run_sparse_validation.py` | 同一 runner 表达容量矩阵并核对 transcript | 默认选择不变，超时默认 600 s 不变；原 field 2e-10、limiter 2e-8 不变；完整方法/步/状态覆盖不变；新增 storage/pool 必须匹配请求，仍要求真实演化、真实 provider 与身份未变。不是新比较器 |
| `validation/network/test_sparse_validation.py` | 扩大 parser 的控制测试 | 原 5 项加容量/错误/旧存档兼容测试到 9 项；合成 transcript 明示只是 parser fixture，不是科学结果 |

审查发现一个需明确的兼容性细节：默认 C++ transcript 格式保持，但 Python summary 会新增 `requested_pool_cells` 字段，不能声称整个 evidence JSON 字节级不变。原字段、预算和判断保留；维护者需审查 schema=1 下增加字段是否符合消费方契约。新结果须记录实际 parser 身份，不能改标旧记录。

## 必须拒绝的情况

- 缺少/重复/修改 storage_controls；请求 32/33 实际报告 32/34 或 pool 不符。
- 缺少 ODE、步骤、最终状态，重复完成记录，非零失败记录，只有 selected ODE 的部分矩阵。
- 超预算、非有限字段、无真实演化、CPU attempts 合计不符、状态范围/组分闭合异常。
- 超出请求的 provider 容量；硬件可合法限缩容量，因此记录 requested 与 actual，不把请求值当实测并发或内存。
- 非法/重复 CLI 参数、整型溢出、损坏产物/身份。这些 C++ CLI 路径仍需实际二进制负向测试；Python 通过不能替代。

现有默认 audit31 四步存档经新 parser 测试仍通过。大网络扩展前先验证未改 harness 的四步，再构建测试扩展、复跑默认矩阵，最后扩大存储和步数。不会因科学轨迹困难缩短物理区间或放宽预算。

## 新 S0 编排与职责

结果局部 `run_s0_checks.py` 只编排现有工具：源/产物身份用 `validation_provenance`，子进程/日志/超时用 `run_arch_with_logs`，基线用原 Sedov recipe。无新 solver、比较器、sanitizer parser、进程监管器或第二个 Validation 根目录。

新增 `tests/tooling/test_hpc_s0_recipe.py` 覆盖命令失败、中断/超时、缺失/损坏产物、重复输出/完成、部分流程、源码/配置/二进制变化以及“无旧汇总 marker 但可独立检查产物”。完整子进程清理和输入/provenance 细节仍由现有 tooling 测试覆盖。旧 `continue_large_campaign.py` 留作失败历史配方，不再作为自动恢复入口。

工具改动以独立提交交给维护者审阅；不同意协议变化时可回退该提交，不牵连生产数学。实际测试日志与最终 diff 随 S0 证据记录提供。
