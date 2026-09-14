# 耦合正式测试前的存储整理

在三种燃烧正式测试全部退出、第一种耦合测试开始前，仅处理已完成的固定版
`coupled-scale-v1` 与 `coupled-critical-b128-v1` 中超过 2 MiB 的后端 TSV 日志。
13 个文件从 741,105,167 bytes 无损压缩至 24,672,505 bytes；服务器原路径保留 `.tsv.zst`。

先核对服务器与本机的 `fixed-science-v1.tar.zst` 完整 SHA 和大小，再逐文件核对
tar 成员、原 SHA 清单、当前文件及解压内容；全部一致后才移除展开副本。
两份原始科学归档保持不变，唯一日志内容、源码、生产产物、其他输出没有删除。
文件列表及原始／压缩／解压 SHA 见 `coupled-preformal-trace-compression-v1.json`。

完整原始科学包：1,601,189,714 bytes，SHA-256
`613405b2b5570bdbb081a95feed5f863b66e699e9afab9a6d1881c79b99abbae`。
服务器在 `ARCH-multiphysics-fix-20260914/build/`，本机在 `C:/tmp/ARCH-perf-20260909/build/`。
本目录保存真实执行配方和清单；压缩不与正式 ARCH 计时并发，没有关闭运行时 trace。
