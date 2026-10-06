# 第二平台身份更正：4070 Ti

用户于2026-10-04明确确认5070 Ti是描述错误，实际为4070 Ti。本地第二平台记录更正为i7-14700K+RTX4070Ti，不改变原完整任务范围、科学定义、参考或预算。

现有只读preflight以期望NVIDIA GeForce RTX 4070 Ti重新执行，exit0/platform_match=true，GPU UUID与此前两次一致，显存12282MiB。独立nvidia-smi name/uuid/compute_cap/driver_version查询为8.9和617.14；[NVIDIA官方GPU表](https://developer.nvidia.com/cuda/gpus)也将4070Ti列于8.9。完整查询留本机，摘要见同名Summary。

联合入口、Jeans/RZ执行细则、主计划及development README同步当前平台名称；对应段落/锚点同步。误报5070Ti引入的SM_120/architecture120/CUDA12.8最低版本要求撤销。后续应核对SM_89及实际工具链，不因此自动升级、configure、新建CUDA tree或修改缓存。此处是后续构建要求，不声称已有CUDA binary已用89且通过验收。

旧PlatformPreflight报告和Summary保持原两次exit2、原期望5070Ti与实际4070Ti的历史证据。本次用户确认解除平台描述差异；不把旧失败改写成旧时点通过，不将新期望回填到旧benchmark或产物身份。

CPU/GPU数值验收、科学Core决策、架构迁移授权及冻结终点仍待完成。瞬时GPU读数不代表idle、峰值或ARCH负载。没有新Build、ARCH/Preview/simulation/CUDA计算；不重复未变化的解析器/Studio/Core基线。只做文档链接/差异校验和新的已确认平台预检，raw留本机，不push/tag。
