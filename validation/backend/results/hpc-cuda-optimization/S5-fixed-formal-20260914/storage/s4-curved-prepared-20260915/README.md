# S4 曲线 HDF 展开副本整理：准备记录，尚未执行服务器清理

日期：2026-09-15（北京时间）。第二组 BE_NR＋RKL2 正式计时期间发现根盘可用空间持续下降。
不减少样本、关闭 trace 或降低下一模块原 8 GiB 启动门槛；也不在采样期间运行归档、清理或新实验。
下面只准备一个可审计的阶段间隙容量处理方案，不是新的科学验收。

## 当前已检查

- [原 S4 归档回执](../../../S4/validation-20260913/evidence/archive-receipt.json)
  记录完整服务器／本机 canonical tar.zst 备份和原始构建、checkpoint、日志。
- 本机重新验证 `s4-validation-20260913-evidence.tar.zst`，1,236,911,980 bytes，
  SHA-256 `ffa2287b0e61c5b9f7090138add48e52b41bc454ba07db872103d1b421b0a493`。
- 归档内曲线矩阵共有 384 个 HDF，逻辑大小 5,229,992,704 bytes。
  [本机独立成员清单](local-proof.json) 的 SHA-256 为
  `dd5f1a40d6add5097305f7b28e23055eb5b25abff2d9cee0a3d6a748be6c5640`。
- 本机通过已验证归档的成员顺序流式读取 HDF 字节并计算 SHA，没有额外展开这 5.23 GB 数据。
- 服务器只读目录／文件大小检查确认该曲线目录仍有同样数量和逻辑大小的 HDF，canonical archive 大小也一致。
  **这不是服务器内容 SHA 校验，尚不构成删除授权门槛通过。**
- [十项本机工具测试](s4-curved-compaction-contract-tests-20260915.log) 通过；
  使用小临时文件、模拟进程和 zstd 解码，不连接服务器，不运行 CUDA 或实际科学轨迹。
  覆盖只读预检、仅移除已验证 HDF、活动作业拒绝、变更／额外数据拒绝、路径越界拒绝、
  旧回执不覆盖，以及部分删除失败时保存 pending／removed 日志。

## 实际执行必须满足

仅在没有 ARCH、编译器或 sparse harness 活动的阶段间隙使用
`compact-verified-s4-curved-20260915.py`。
目标严格限于 `/home/ubuntu/projects/ARCH-hpc-s4-validation-20260913/build/s4-validation-20260913/curved/`。
当前 FIX／BASELINE Release、ORIGINAL、旧 device factory、BE 待用资产均不在范围内。

工具必须重新验证本机清单身份、服务器 canonical archive 的完整 SHA、
tar 内各 HDF 成员和服务器 live HDF 的字节、哈希及完整清单，三者全一致后才能移除展开副本。
不得删除唯一数据、任何原始归档、源码、二进制、日志或目录；不改他人的文件。
实际可用空间变化由执行回执记录，不能仅凭 HDF 逻辑字节数声称已释放对应物理空间。

所有移除操作均先保存 pending 记录，再保存 removed 结果。
旧回执存在或部分失败时必须停下检查，不覆盖记录、不盲目重跑。
当前脚本默认只读验证也会生成回执；同一默认回执路径不能先 dry-run 再直接 apply，
需要先审查该次记录并为后续尝试使用独立受审查版本。

此处只是准备副本。服务器尚未上传／执行整理脚本，尚未释放任何文件。
采样期间仅执行少量 ps／df／tail 和文件大小元数据读取，用于运行与容量安全检查。
若容量低于 2 GiB，当前本机监视循环只发出警报，不自动停止进程；
随后应先检查并安全停止**我们自己的**实验（如确有必要），保存中断记录，
在无活动采样后完成容量处理，再按明确保留原尝试的协议恢复。
