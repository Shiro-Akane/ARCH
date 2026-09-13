# V1：大网络 BD 原矩阵残差回归修复

日期：2026-09-14。状态：设备驻留修正已实现；audit150/200 原四步和附加失败合同均通过。本项不是全应用、长轨迹、容量或安全验收，不代表燃烧性能已优化完成。

## 依据与实现

[前置诊断](../burn-status-20260913/README.md)在完全保留 Host 网络/BD 数学时，仅换成原 cuDSS provider 便复现 ENUC 差异：原系统残差检查额外拒绝线性解，改变 BD 接受/拒绝轨迹。KLU 重做 numeric factor 或测试用高精度残差修正不能解释原差异。没有修改外部 KLU。

本候选保留 cuDSS 0.8 的原配置（含既有两次 native IR），并在 provider 内加至多两轮**原系统**残差修正：

1. 使用共享 `SparseResidual.h` 的固定顺序 compensated double/FMA 构造 `b-Ax`；接受表达式保持原 componentwise residual 门槛。
2. 残差、修正向量留在 Device，复用同一 factor/token；不下载状态/矩阵交给 CPU。
3. 将 residual 状态整数并入既有完成回传和 fence。正常矩阵只多一个 residual kernel，无额外同步；只有可修正的失败才有界重试。
4. 非有限/不可修正或两轮后仍不准确的结果，继续由原 ODE 最终残差检查拒绝；不删除 native 错误传播、EOS latch 或原发布边界。

ODE、RHS/Jacobian、反应率、质量/能量权威、DenseLU 分界、科学输入、浮点选项、库和 restart schema 均不变。没有新增 provider、CPU fallback 或全局库安装。

## 验证组织与边界

原大网络构建位于 `/home/ubuntu/projects/ARCH-perf-20260909/build/large-network-20260909/release`，源码 `0266d96f`；它不是 S4 Sedov 构建。候选源放在独立 `build/provider-residual-20260914/overlay-v2/`，两个 provider TU 重编，原 150/200 factory 对象原样重链接。完整 compile/link 命令、源码/库/对象/构建身份写在 candidate record；不覆盖原二进制、不把其身份重标为本地提交。

旧 heavy factory 两个 TU 编译约 3575/6756 秒，本次 private provider 变更没有要求再编它们。该 ABI-compatible 增量实验不能替代最终 clean/full build。

首轮 candidate 已完成编译/链接、Host 数学合同，随后在旧 provider 流量计数断言失败，未启动 150/200。原因是新 residual kernel/状态整数未被旧计数包含；第二轮仅重编该工程合同，精确计数更新为 factor+solve 5 个/复用 solve 3 个显式 kernel，首次 analysis+factor+solve 回传 4 个 int、复用 solve 2 个 int、再 factor+solve 3 个 int。科学精度与容差断言没有变化。首轮失败原始记录保留。

| 检查 | 当前结果 |
|---|---|
| 共享 Host 残差数学（含抵消、乘积余项、无效输入） | 通过 |
| 原 cuDSS provider 制造矩阵、reuse/refactor、负向合同 | 通过（第二轮）；无 skip |
| audit150，BE_NR/BD/ROS4，2/3 单元各四步 | 通过；最大场误差分别 6.057e-15 / 1.796e-14 / 8.518e-15；原预算 2e-10 |
| audit200 同矩阵 | 通过；最大场误差分别 5.315e-15 / 1.852e-14 / 1.191e-14；原预算 2e-10 |
| 本地源架构审计及对应工具测试 | 审计通过、98/98 通过 |
| 附加 sparse continuation / EOS 失败合同 | 2/2 通过；原失败回滚、残差拒绝和 EOS latch 测试未改动 |
| 完整应用/容量/长期、sanitizer、clean 构建 | 未由本项验收；vGPU 调试阻塞仍在 |

150 的诊断时间 BE_NR：CPU 12.45 秒/GPU 375.28 秒；BD：0.114/4.091 秒；ROS4：0.338/10.408 秒。仍然是 2/3 单元测试，不能作为 CPU8/16 全网格性能对照。本项修复正确性，不能宣称解决了燃烧慢速。

200 的对应诊断时间为 17.56/527.64、0.147/4.536、0.504/14.478 秒，同样不属于正式性能样本。候选完整诊断约 1000.3 秒，整设备显存峰值 14528 MiB；附加合同约 79.0 秒、3751 MiB。两轮都没有触发内存保护、swap 增长或观测到 OOM。

## 独立资源探针

完成无插桩回归后，另用 `validation/network/cuda_burn_kernel_probe.cpp` 的诊断 interposer 查询每个已观察 ARCH launch entry 的 runtime attributes；不是 kernel 计时，不属于正式速度样本，也不是 sanitizer。仅运行 audit150 的原 BD 四步，parity 再次通过，前后源码/探针/二进制 hash 一致。

`advance_ode<audit150, BD, IdealGasView>` 报告每线程 local memory **47152 bytes**、**255 registers**，该微测试最大 launch 为 **1 block × 32 threads**，观察到 756 次调用。符号通过同一个二进制的 `nm -n -C` 地址精确对应；所有 attribute query 返回成功。显存整设备峰值为 10819 MiB。这支持优先检查 ODE continuation 的局部状态、寄存器压力和小批量利用率；不能由单个属性值直接推出整设备内存归属、spill 次数、内存泄漏或耗时占比。

可复现命令（在冻结服务器工作区执行；运行前确认没有其他 GPU 任务且 `LD_PRELOAD` 为空）：

```bash
root=$PWD/build/provider-residual-20260914
g++-11 -std=c++20 -O2 -fPIC -shared \
  -I/home/ubuntu/projects/.envs/arch/targets/x86_64-linux/include \
  "$root/cuda_burn_kernel_probe.cpp" -o "$root/kernel-probe/probe.so" -ldl -pthread
build/network-python-20260909/bin/python tools/run_memory_guarded.py \
  --min-available-mib 16384 --max-swap-growth-mib 0 --pressure-guard \
  --gpu-memory-device 0 --log "$root/kernel-probe/run.log" -- timeout 120 env \
  LD_PRELOAD="$root/kernel-probe/probe.so" \
  ARCH_CUDA_KERNEL_PROBE="$root/kernel-probe/attributes.tsv" \
  "$root/candidate-v2/arch_cuda_generated_sparse_burn_audit150" \
  1e7 3e9 1e-10 1e8 1e-7 4 --ode bd c12=0.5 o16=0.5
```

## 证据与归档

- `raw/candidate/`：首轮工程计数断言失败及完整命令；没有删掉失败记录。
- `raw/candidate-v2/`：完整编译/链接命令、原构建 provenance、overlay/二进制 SHA、两网络原始逐步状态和比较摘要；原基线前后身份检查通过。
- `raw/extra-contracts/`、`raw/kernel-probe/`：附加合同、资源属性及身份记录。`raw/final-identity-check.log` 再检查候选源、产物和附加合同 provider 绑定。
- `local/`：源架构审计与对应 98 个工具测试输出。
- 完整源/对象/二进制和日志包保留在服务器 `/home/ubuntu/projects/ARCH-perf-20260909/build/provider-residual-20260914-evidence.tar.zst` 及本机 `C:/tmp/ARCH-perf-20260909/build/provider-residual-20260914-evidence.tar.zst`；20203386 bytes，SHA-256 `7f567797b925e742ac73e31307e4f1db65405fad77f1a873fc90d47626442e96`。不向 Git 塞入可执行文件。

后续执行顺序仍是总计划中的有界容量与应用验证、燃烧调度/矩阵性能、扩散批量化、四模块耦合验收。检查到的逐块 diffusion dt/stage/copy 等待及单 factor owner 的 lane 驱逐，只是下一轮候选热点，不冒充已获得收益。
