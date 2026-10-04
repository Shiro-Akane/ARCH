# 第二平台身份预检：GPU 与计划不一致

git fetch 后 compute/optim 仍为8fc0dd25eefd2243e8c36f85440bac46994e2e73；codex/o8-boundaries新增10caebe77079e26c0e1e7bc76a1c7c75b5fb4531，只记录RT共用修复集中维护，不含待决Jeans/RZ科学定义、预算或架构规则迁移批准。未merge/rebase。

新增 validation/gravity/curved/platform_preflight.py，只读Linux/WSL瞬时CPU名称、可用逻辑CPU、guest内存、frequency-policy可用性，以及nvidia-smi whole-device memory/power/temperature/utilization。缺失指标为unavailable+reason，不置零；不读取环境秘密或修改硬件设置，不执行CUDA workload。

两次实际预检均exit2：计划NVIDIA GeForce RTX 5070 Ti，实际仅NVIDIA GeForce RTX 4070 Ti、同一UUID、总显存12282MiB。CPU为i7-14700K，可用逻辑CPU0..27，不推断物理核/P-E绑定。WSL没有暴露cpufreq policy，明确unavailable。具体瞬时值及工具SHA见Summary，不是峰值、idle证明或ARCH归因。

当前GPU结果不能作为冻结5070Ti验收。已询问用户5070Ti是否尚未接入、位于另一机器或计划需更正；没有自行更改计划/编译架构/阈值，CPU继续，正式CUDA身份仍待冻结。

3/3解析测试PASS：未知值非零替代、NaN/坏值/越界/重复身份拒绝、多设备身份独立。初次多设备fixture误用了字面反斜杠n，2PASS/1ERROR日志保留；只修fixture换行，再测3PASS。不能把该初次fixture错误描述为真实设备失败。

此工具只是资源观测预检，不是连续采样；RSS、峰值、CPU频率、正式运行隔离和计时仍未完成。没有Build、ARCH/Preview/simulation/CUDA计算或长跑，不重跑不相关runner/Studio基线。完整快照/日志留本机ignored目录；只提交脚本和处理后摘要。不push/tag，联合目标未完成。
