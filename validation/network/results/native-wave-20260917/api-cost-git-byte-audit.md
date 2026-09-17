# API 成本证据的 Git 库存／字节核对

核对提交：`f0f31c3ec6bf6df82c41d586dece6500e30fb6c1`，2026-09-17。
范围：`validation/network/results/native-wave-20260917/api-cost-v1`。

工作树完整库存 60 个文件、425,948 bytes，与 `git ls-tree -r` 逐路径一致；
每个文件的 `git hash-object --no-filters` 与提交 blob ID 相同。
没有遗漏嵌套证据文件，也没有对原始日志换行做归一化。

个人 Arsenic-er 和朋友 Shiro-Akane 的 `codex/hpc-cuda-optimization`
均已通过 `ls-remote` 核对为上述同一 SHA。没有推送 main、origin 或强制覆盖。
本项只验证交付完整性，不增加 GPU 运行数、数值或性能资格。
