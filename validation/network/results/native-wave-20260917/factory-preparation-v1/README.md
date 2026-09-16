# 全新 factory 编译配方与首次准备失败

新服务器根 `/home/ubuntu/projects/ARCH-native-wave-v4-20260916`，独立 source（8da9b23d detached worktree + 已通过 standalone 的 v4 overlay）和 `factory-release`。只构建 audit150／audit200 的真实 factory 与 provider，串行、原 strict FP、同版本 KLU/cuDSS；不借用旧 factory 对象。

`initial-failed-setup/` 是 worker 214936 的完整终态，exit 1。初始安装在修改任何源文件或开始编译之前，因 Linux LF 与本机混合 CRLF 的原 SHA 不同而拒绝。`install-...-initial` 保留原严格检查，v2 先核对 pinned Git 原文，再逐字节比较只去 CR 的共享数学文本，最终安装的是已测试 payload 原字节。不存在改变公式或对证据换行归一。

新 worker 215489 使用 `factory-control-v2`；这份配方快照不声称编译或实际 ODE 通过。初次建立的新 worktree 和 overlay 已核验且尚未修改，因此接续同一个全新源码树；build 在接续前仍不存在。全部新编译对象留在独立新 build 中，旧产品不覆盖。

每次编译保留完整命令、elapsed 和 peak RSS；总构建 12 小时护栏，Host available 至少 32 GiB、swap 增长至多 64 MiB、系统压力与 GPU 观测。两 factory 的磁盘入口为 4 GiB；这不是全 ARCH 构建的资源资格。完成后先核对源码／网络／vendor／产品 SHA，再启动实际三 ODE 数值门槛，无自动性能采样。
