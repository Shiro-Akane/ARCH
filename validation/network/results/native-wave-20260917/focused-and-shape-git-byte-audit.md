# focused 与布局诊断的 Git 原字节核验

2026-09-17，对提交 `4405859ebb33d8733c638171d99694331a2836e2` 的完整
`batch-launch-focused-v1/`、`batch-launch-focused-reaudit-v1/`、`advance-shape-v1/`
目录核验：210 文件、4,476,661 bytes。
工作区普通文件清单与 Git tree 一致，全部 `git hash-object --no-filters`
结果等于 Git blob ID，包含嵌套build路径证据；未归一化原始换行。
压缩包和原文件 SHA-256 验证另见各目录本机回执。
该提交在个人 Arsenic-er 与朋友 Shiro-Akane 的固定分支
`codex/hpc-cuda-optimization` 上已核对同 SHA。
