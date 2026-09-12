# S4：Host 调度优化与公平对照准备（开发快照）

日期：2026-09-13。承接 S3a `964c0285`、S3b `12b008f3`；S2 的空 boundary phase/尾批修正单列 `57b32e49`。

## 实施范围

- Host 同层 exchange 复用编译计划及 gather/scatter scratch。缓存仅保留逻辑端点、布局和操作，不保留字段指针。
- 每次重新构造当前 slot 的 Host views，检查当前指针、布局、species、handle。逻辑计划重建时清除 Host 编译缓存；合法新布局重新编译，非法布局拒绝。
- 原执行器的全计划预校验、phase 顺序、逐元素复制与先 gather 后 scatter 不变；没有因缓存命中而省略错误检查。
- Driver 的 CFL 候选、Hydro access、boundary access/分层 access 数组保留容量；每次清空并用当前 generation/slot 重填。
- 逻辑缓存比较用的临时 key 保留容量，减少 Host 反复分配。
- 不改 CPU 数学、OpenMP 调度策略、依赖、浮点编译选项、restart schema。缓存/工作区属于串行调度实例，不是全局共享状态；同一实例不支持并发或重入调用。

## 短检查

MSVC 19.44 x64 Debug 窄工程，6/6 Host 合同通过；架构工具 98/98 通过，源码审计退出码 0。原始命令输出和窄工程配置保存于 evidence/。

覆盖：三个 slot 的不同字段值、替换 Host 存储地址后重新绑定、非法布局拒绝且不替换旧缓存、species/epoch 失效、scratch 容量复用。原 bitwise copy 检查继续覆盖特殊浮点值。

新增测试首次失败：误把 `Grid::Ie()` 排他上界加一后当作第一层 ghost，导致期望 source 偏一格。核对 Grid 契约后只修正测试索引，随后 6/6 通过；未改公式或容差。

## 待执行的公平对照

按用户要求，本轮**不运行**线程扫描、长 GPU 验证或性能实验。因此 S4 的实现和对照流程已准备，公平对照验收尚未完成；没有新的加速比。NVCC/完整 ARCH 集成、GPU 数值、sanitizer 与内存峰值也仍待测。

沿用现有 `validation/backend/results/maintenance-freeze-20260908/run_sedov_amr_timing.py`，不另写比较器或改变原预算。待同一提交的 Release ARCH 与 checkpoint validator 可用后，在无其他重型任务的 Linux 服务器串行运行：

```bash
# 将两个路径设为同一源码提交、同一构建目录的真实产物。
ARCH_BIN=/absolute/path/to/build-release/ARCH
CHECKPOINT_VALIDATOR=/absolute/path/to/build-release/arch_cuda_single_level_validation
RUN_ID=s4-thread-scan-UNIQUE_ID
for threads in 1 2 4 8 16; do
  python3 validation/backend/results/maintenance-freeze-20260908/run_sedov_amr_timing.py \
    --arch "$ARCH_BIN" --checkpoint-validator "$CHECKPOINT_VALIDATOR" \
    --blocks 4 8 --levels 2 --time 0.02 --threads "$threads" \
    --backend cpu cuda --warmups 1 --repeats 5 --timeout 1200 \
    --output-root "build/$RUN_ID/threads-$threads" || exit "$?"
done
```

产物名称需按实际构建确认，不以占位路径作为构建/运行证据。输出必须是 checkout 内新的空 build 子目录，不覆盖已有基线。

| 条目 | 固定／记录要求 |
|---|---|
| 输入 | 原两档 4×4、8×8；HLLC/PPM/RK3，CFL=0.4，AMR max=2，regrid=2，t=0.02；不降低工作量 |
| 线程与亲和性 | 1/2/4/8/16；现有脚本固定 OMP_DYNAMIC=FALSE、OMP_PLACES=cores、OMP_PROC_BIND=close。保存 lscpu/cpuset/NUMA/SMT 信息，不把 VM vCPU 数当物理核心数 |
| 配对 | 每个线程数均重新运行同轮 CPU/CUDA，warmup 不计入，测量串行交错；脚本会调用原比较器，不跳过数值失败 |
| 汇总 | 每档、每线程数完整原始样本、中位数、离散性、步数/regrid 序列、守恒/场误差、构建与硬件身份 |
| 两种比值 | 旧 CUDA/新 CUDA 与同轮新 CPU/新 CUDA 分开；原 8 线程对照必须保留，可另列最佳 CPU 线程点 |
| 调度变化 | 若后来试 spread 或改变 CPU 算法/OpenMP 策略，作为独立实验修改，重新做 CPU/CUDA 对照，不能混入本轮 close 数据 |

本轮没有启动上述命令，没有上传新代码到服务器，也没有创建自动继续任务。S5 Graph、kernel 深调和大网络专项不在本次实施范围。

## 交付

本地、朋友 `Shiro-Akane/ARCH` 和个人 `Arsenic-er/ARCH` 使用 `codex/hpc-cuda-optimization` 开发分支。2026-09-13 的 LFS 补传已解除历史表缺失阻塞，双方分支已核对一致；详见下方恢复记录。同步完成不代表 GPU/性能验收通过。

两个 main 保持不变，不建立未验收标签。短 Host 检查通过不代表 CUDA 或性能验收通过。

### 首次推送回执（历史失败，现已解决）

S4 代码和检查记录提交为 `1a850c3581231c3cf16020ebe14ae3bc8ab25a96`。朋友固定分支已正常 fast-forward 到该提交，GitHub API 核对一致。个人端正常 push 退出码 1：24/26 历史 LFS 对象已在目标端，仍缺 eos2.tab/eos4.tab 完整本地内容，分支尚未创建。

原始回执见 [朋友 push](evidence/push-friend.log) 与 [个人 push](evidence/push-personal.log)。本段与回执归档是文档后续提交，不改变受短检查覆盖的代码；当时个人端状态为部分同步，以下恢复记录替代这一过期状态。

### LFS 补传恢复（2026-09-13）

按用户要求，从朋友仓库以正常 `git lfs fetch` 恢复 eos2.tab/eos4.tab 到本地 LFS 缓存。两份对象的字节数和 SHA-256 均与提交指针一致；服务器既有副本也重新核对一致。没有重建或修改 EOS 数据。

正常 push 到个人固定分支已成功，退出码 0；LFS 回执为 `100% (26/26), 287 MB`。个人 LFS download-batch 查询确认两份对象可用。个人固定分支随后成功创建，双方 API 均返回 `cb86c6097652d04afe393c3df7e0a3241275c149`。

身份与校验结果见 [恢复回执](evidence/lfs-recovery-20260913.json)。本恢复说明作为后续文档提交同步到同一双端分支，最终 SHA 以 Git 回执为准。未跳过 hook/LFS 完整性检查、未强推、未改两个 main，未改生产代码或测试预算，也未补跑实验。

### 服务器数值验证（2026-09-13）

用户随后允许将 `c44a183c` 同步到 GPU-273312 独立目录验证。完整 backend/ARCH 构建成功，最终 24/24 合同与设备测试、121 组 checkpoint 对比通过，覆盖整波/尾波、原 AMR/曲线矩阵、长期 regrid 和双向 restart。仅修正一个未实际触发 EOS 错误的测试输入，生产数学/库/预算/schema 未改。vGPU 禁止调试，sanitizer 安全未验收。用户明确要求计时稍后再做，本轮未运行 pilot、线程扫描或 profiling，也没有新的加速比。完整状态和证据见 [本轮验证记录](validation-20260913/README.md)，不发验收标签；此前“不运行实验”仅保留为开发快照历史。
