# Git 原字节核验

2026-09-17 对提交 `526596288662f5ab4a7b531f229c8a1f3ae22f12` 的两个完整目录
`capacity-v2/`、`batch-launch-contract-v1/` 核验：165 文件、3,041,338 bytes。
工作区递归普通文件清单与 Git tree 完全相同，包含被通用 build 规则忽略后显式加入的证据。
`git hash-object --no-filters` 逐一等于该提交的 blob ID，未归一化原始换行。
两份完整压缩包／所有原文件的 SHA-256 检查另见各目录的本机回执。

该提交已在 Arsenic-er 与 Shiro-Akane 的 `codex/hpc-cuda-optimization` 分支
通过 `ls-remote` 核实同 SHA；未修改任一 main，也未使用 force push。
