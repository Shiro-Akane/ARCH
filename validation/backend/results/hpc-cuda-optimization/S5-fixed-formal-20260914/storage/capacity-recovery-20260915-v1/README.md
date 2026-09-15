# 阶段间容量恢复：实际执行与保全记录

本目录是已经执行的存储操作证据，不是新的物理或性能通过记录。
日期采用 UTC：S4 整理发生于 2026-09-14 22:26:45–22:27:28，
BD/RKL1 未启动 preflight 的保全于 22:29:02 完成。
操作在第二 BE/RKL2 完整采样、归档之后，且没有 ARCH、编译或 sparse harness 活动时进行。
后四组接续在 2026-09-15 05:12 UTC 左右重新检查身份、空闲和原容量门槛后启动。
期间存在本机工具会话中断，没有把这段间隔描述为持续采样，也没有重跑前七组。

## 原因与处理范围

第二组完整通过后，第三 BD/RKL1 的 worker 3874056 在 preflight 非零退出。
当时磁盘可用不足原 8 GiB 要求，源码／产物 SHA 检查通过，GPU 空闲。
没有生成 sample root、样本 stdout/stderr 或 archive；完整 preflight 日志原样保留。
这不是运行中的数值失败，不能算一条通过样本。

整理范围仅为旧 S4 `ARCH-hpc-s4-validation-20260913/build/s4-validation-20260913/curved/`
中 384 个已双端归档的冗余展开 HDF。原 canonical archive、源码、二进制、日志和目录不删除，
当前 FIX／BASELINE、BE 待用 factory、其他用户进程和文件均不在处理范围内。

执行前验证本机成员证明、服务器完整 archive、archive 内成员和 live HDF 的全部 SHA 及清单。
全部通过后逐文件记 pending／removed 日志并移除重复展开副本；结束后再次核验服务器原 archive SHA。
不存在唯一数据删除或凭文件名／大小推断内容相同。

## 实际结果与身份

- [S4 执行回执](s4-curved-compaction.json)：applied，384 个核验成员与 removed 清单一致，pending 为空。
- 冗余 HDF 逻辑大小 5,229,992,704 bytes，实际 allocated 合计 5,230,919,680 bytes。
- 文件系统实际 free 从 4,785,303,552 增至 10,016,047,104 bytes，约 4.46→9.33 GiB。
  不以逻辑字节代替实际释放容量；后续接续仍检查原 8 GiB 门槛。
- 原 S4 raw：1,236,911,980 bytes，SHA-256
  `ffa2287b0e61c5b9f7090138add48e52b41bc454ba07db872103d1b421b0a493`，服务器和本机原包均保留。
- [本机成员证明](operation/local-proof.json) SHA-256
  `dd5f1a40d6add5097305f7b28e23055eb5b25abff2d9cee0a3d6a748be6c5640`，原 CRLF 字节保持不变。
- 原未启动记录的十个文件全部保全至 `preserved-preflight-coupled_bd_rkl1_all_transport-20260915-v1/`，
  七个顶层移动项完成、pending 为空；原文件字节逐项验证。副本在 [preflight](preflight/)。
- 未启动记录 raw：`preflight-coupled-bd-rkl1-20260915-v1.tar.zst`，4,632 bytes，SHA-256
  `568c82a7b38b65e351fcb29e3f82c8b1dec232a054e5d85caa011db12b3b712a`，已双端核验保存。

执行配方来自已双端发布的 `20f388dd8a81fa9fb2761d3651e5db5d89740fb7`；
四个 Python 文件逐一匹配该 Git blob。上传 bundle 为 139,264 bytes，SHA-256
`8f1dcf3f6b5fb4e46b9640f9ab185620b35f2f92e2ef2266cc5cd7182d0e1107`。
[实际源码身份](operation/source-identities.sha256)、Python 版本、开始／结束时间、stdout/stderr、
归档后验 SHA 和剩余 HDF 空清单均保存在 operation 中。

## 接续资格与边界

服务器实际执行了 12 项 S4、13 项 preflight 的临时 fixture 测试，共 25 项通过；
它们只验证整理工具，不是 CUDA／物理／sanitizer 测试。
[本机回读核验](verification.log) 检查源码和证明 SHA、384 文件 ledger、原包后验 SHA、
十个保存文件身份、测试日志与前七组正式记录。
[七组完整性审计](seven-formals-before-capacity.json) 为 756 次运行、735 次比较通过。

一次性接续入口只启动尚未采样的 BD/RKL1、BD/RKL2、ROS4/RKL1、ROS4/RKL2，
沿用原 v3、全部输入／精度／样本数和预先登记的 3600 秒 wall 上限。
四组之后才进入原 BE 长轨迹补测。这里不把未来四组或长轨迹标为通过。

整理和保全工具已经完成，回执为不可覆盖的新版本记录；不得盲目重跑。
本目录 `* -text` 保持所有回读证据的字节身份。
