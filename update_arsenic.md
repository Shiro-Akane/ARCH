# ARCH CUDA 移植 · 更新日志（update_arsenic）

> 本文件记录 `cudab4burn` 分支上每一版的改动：**更新了什么、上传了哪些文件、各文件是干嘛的**。
> 约定：最新版本写在最上方；每版含「目标 / 结果 / 新增文件 / 已知问题 / 下一步」五节。

---

## v2 — 2026-07-12 · GPU 变温燃烧内核 · 对齐作者完整 Burner

### 目标
作者在 main 补全了变温燃烧（温度随核能演化 `dT/dt = enuc/cv` + Helmholtz EOS 查表算 cv，
17 维系统 = 16 组分 + 温度），并修复了我方 v1 报告的全部 4 个 bug。
把我方 GPU 燃烧内核从 v1 的**等温**升级到**变温**，与作者的完整 Burner 逻辑逐位对齐后做 CPU/GPU 对比。

### 结果
- **正确性**：262,144 格点，CPU/GPU 同源同初值，最大相对误差——组分 1.6e-6、**温度 9.2e-12**，
  自适应子步数逐格点完全一致（115.7）。
- **性能**：CPU 16 线程 46.7 s vs GPU H100 15.3 s = **3.1× 加速**。
- 加速比 v1 等温版（6.8×）低有物理原因：变温版每个牛顿迭代多 ~36 次 Helmholtz 表插值
  （大量 pow/log 超越函数，GPU SFU 吞吐受限）+ 17×17 雅可比占 2.3KB/线程 shared memory，压低 occupancy。

### 本版新增/更新文件
| 文件 | 说明 |
|---|---|
| `cuda/burnbench_v2.cu` | 变温版 GPU 燃烧基准。相比 v1：NEQ 16→17（加温度）、温度方程 `RHS[16]=enuc/cv`、17×17 雅可比温度行列有限差分、`HelmEos` 的 `calc_thermo/get_cv`（f[9] 表 7.8MB 上传 device + 五次 Hermite 插值）全部 `__host__ __device__`。照抄作者新 `ode_be-nr.h` / `HelmEos.h`。 |
| `cuda/RESULTS.md` | 追加变温对比结果。 |

### 已知问题 / 说明
- `cuda/burnbench_v2.cu` 依赖 device 标注版 aprox19 头文件（用 sed 从原网络自动生成，方法见 `cuda/RESULTS.md`）；
  另需把 `network::mion`（host-only `inline Array1D`）改为 device 可访问——基准里用镜像常量 + 自实现 `compute_enuc` 绕过，不改原码。
- 跑基准需 `helm_table.dat`（60MB，见 v1 说明的下载方法）。

### 下一步
- 内核优化：Helmholtz 表用 texture/`__ldg`、缩小雅可比 shared 占用提高 occupancy、warp 协作，冲更高加速；
- 把 GPU burner 真正接入主程序 Driver（替换 `do_burn_step` 的 OpenMP 循环），端到端跑 Cellular 爆轰；
- 之后进入 block-structured AMR。

---

## v1 — 2026-07-12 · CUDA 燃烧内核可行性与正确性验证

### 目标
把 ARCH 的核燃烧计算（aprox19 反应网络 + 刚性 ODE 求解器）从 CPU 搬到 GPU（H100），
并与 CPU 逐位对比，验证 **GPU 加速在这个项目上既正确又值得做**。这是整个 CUDA/AMR 大项目的第一块试金石。

### 结果
- **正确性**：CPU 与 GPU 跑同一份求解器源码、同一初值，100 万个网格单元的燃烧结果一致到机器精度
  （最大相对误差 **9.6e-14**，每个单元的自适应子步数完全一致）。
- **性能**：1,048,576 个单元、等温 16 组分后向欧拉 + 牛顿迭代燃烧，
  **GPU 3.41 s vs 16 核 CPU 23.3 s = 6.8× 加速**。
- **环境**：GPU 为 NVIDIA H100-20C（114 个 SM，20 GB 显存，sm_90），CUDA 12.4，gcc 11.4。

### 本版新增文件

| 文件 | 是什么 / 干嘛用 |
|---|---|
| `cuda/burnbench.cu` | **核心成果**。把 ARCH 的 BE-NR 刚性 ODE 燃烧求解器（`src/numerics/burnsolver/ode_be-nr.h`）逐行移植为 `__host__ __device__` 版本：一个 GPU 线程算一个网格单元，雅可比矩阵放进 shared memory 并做 lane-interleave 消除 bank conflict。同一份源码编译出 CPU(OpenMP) 与 GPU 两条路径，直接对拍。 |
| `cuda/RESULTS.md` | 上面基准的结果记录 + 优化历程（1.0× → 5.7× → 6.8×，每一步做了什么、瓶颈在哪）。 |
| `cuda/smoke.cu` | 最小 CUDA 冒烟测试（saxpy + 打印设备信息），用来确认 H100 工具链可用、显存带宽正常。 |
| `ARCH_背景与学习路线.md` | 面向「有 CUDA/ML 基础、但没有 CFD/天体物理背景」的读者的补课文档：项目背景 + 六大知识域（可压缩欧拉方程、有限体积/Riemann、物态方程、核反应网络、AMR、GPU 优化）+ 逐章学习路线与代码对照表。 |
| `ARCH_burn_bugs.md` | 通读燃烧模块源码时发现的问题清单（见下「已知问题」），含 `文件:行号` 证据链与修复建议。 |

> 说明：GPU 内核直接调用原仓库里 pynucastro 生成的 `actual_rhs`/`actual_jac` 本体，
> 未修改任何 ARCH 源文件；`aprox19_gpu/` 那份 `__host__ __device__` 标注镜像是用一行 `sed` 自动生成的，
> 属于中间产物，未上传（生成方法见 `cuda/RESULTS.md`）。

### 已知问题（详见 `ARCH_burn_bugs.md`）
移植过程中发现燃烧路径有 4 处「接线」不一致，导致**目前燃烧在主程序里其实一步都没真正执行过**：
1. 温度写入 `Y_ODE[19]` 但求解器从 `Y_ODE[18]` 读 → 点火判据永远不满足，燃烧被静默跳过（根因）；
2. `ODE_NEQ=19` 但只填了 `RHS[0..15]` → `RHS[16..18]` 是未初始化内存却参与牛顿迭代；
3. Cellular 算例的组分注册顺序与网络内部顺序不一致（纯碳会被当成纯氦）；
4. 核能生成率 `enuc` 算出后被丢弃、燃烧过程温度不演化 → 无自加热，爆轰无法自持。

这些是 `feat(burn)` 那次提交里未完工的部分，不影响本版 GPU 内核的验证结论（内核绕过了这些接线层）。

### 下一步
- warp 协作版内核（32 线程合算一个单元，进一步提速）；
- 生成白矮星 Helmholtz 4D EOS 表（需 `helm_table.dat`），解锁 Cellular 爆轰算例做端到端对比；
- 流体（hydro）内核 GPU 化；
- 最终目标：在 CUDA 框架上自研 block-structured AMR。

---

<!-- 下一版从这里往上加：## v2 — 日期 · 标题 -->
