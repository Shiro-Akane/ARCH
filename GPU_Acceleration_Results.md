# GPU 加速实验记录 (ARCH Reactive Hydrodynamics)

将 ARCH 的反应可压缩流体求解器移植到 CUDA，实现 **流体 + 燃烧全 GPU 驻留**，
并与 CPU (OpenMP) 做等数据规模的性能对比，随后用 Cellular 碳爆轰算例检验物理。

## 环境
- GPU: NVIDIA H100 (sm_90)，nvcc 12.4，`-std=c++20 --expt-relaxed-constexpr`
- CPU 对照: 16 线程 OpenMP
- EOS: Helmholtz 真表 (`helm_table.dat`, 60 MB, Timmes/cococubed)
- 对拍协议: 同一 `.par` 双跑，hydro L∞、燃烧组分逐格点比对

## 1. 燃烧内核 (独立基准 burnbench)
BE + Newton-Raphson 稠密 LU 刚性积分，核素表进 `__constant__`，
雅可比进 shared memory (lane-interleave 消 bank conflict)。

| 算例 | 规模 | CPU 16T | GPU | 加速 | 最大相对误差 |
|---|---|---|---|---|---|
| 等温 16 组分 | 1,048,576 格点 | 23.3 s | 3.41 s | **6.8×** | 9.6e-14 |
| 变温 + Helmholtz | 262,144 格点 | 46.7 s | 15.3 s | **3.1×** | 组分 1.6e-6 / 温度 9.2e-12 |

瓶颈: 每线程 ~2 KB 雅可比压低 occupancy (~3 warp/SM)，FP64 `exp` 计算受限。

## 2. 全 GPU 驻留求解器 (arch_gpu)
PPM 重构 + HLLC (Glaister) + SSP-RK3 + Helmholtz (牛顿反演) + 变温 burn
全部 GPU 驻留，**每步仅回传 8 字节 dt**。

- **正确性** (2000 全耦合步): rho relL2 = 1.05e-4，组分 X = 1e-14
- **性能** (每步全物理): nx=1000 **15.1×**，nx=16000 **102.8×** (GPU 随规模近乎平坦)
- **Amdahl 拆分** (nx=1000): 流体 + Helm EOS **80×** (占 GPU 时间 8%)，
  燃烧 **9.1×** (占 **92%**) → 燃烧是整机加速的瓶颈

## 3. 燃烧内核优化史 (2D 1000×100, 真表, ms/步)
| 版本 | 优化 | ms/步 | 累计 |
|---|---|---|---|
| v0 | 基线 | 141.1 | 1.00× |
| vB | 表节点交错 | 137.1 | 1.03× |
| vC | 解析温度行雅可比 (19→3 次评估/迭代) | 75.4 | 1.87× |
| vD | 每格点温度热启动 | 71.9 | 1.96× |
| vE | 融合 rhs + jac (单次 evaluate_rates) | 57.7 | **2.45×** |

## 4. aprox13 网络移植
- 将 pynucastro-cxx `aprox13` (13 组分, He4→Ni56) device 化:
  inline 函数加 `__host__ __device__`，核素表 (aion/zion/mion) 做 `__constant__` 孪生。
- device 单元测试通过: 碳燃烧产物合理，解析雅可比对角稳定，全 finite。
- 接入全求解器，初始组分**按名映射** (`spec_names` 查槽) → 换网络自动适配。

## 5. Cellular 碳爆轰算例发现
2560×256 (dx = 0.1 cm)、He4/C12 燃料、真表、aprox13:
- 燃烧化学正确、因果 (未燃区保持 C12 燃料，已燃区按网络产物演化)。
- 但爆轰波 **过驱动** (~10× CJ)，已燃区过热 (eint ~1e19)，反应区弥散于整个已燃区，
  前沿无强激波 → **非自持 CJ 胞格**，而是强初始扰动造出的过热区膨胀。
- **结论**: 干净的 CJ 胞格爆轰需要 **更细分辨率 / block-structured AMR** 或
  **ZND/CJ 剖面初始化** (替代方波式强 Von Neumann 扰动)，
  单纯提高均匀网格分辨率不足以解析反应区。

## 总结
GPU 化实现 **流体 50–100× / 刚性燃烧 3–9×** 加速；燃烧为 Amdahl 瓶颈，
且爆轰反应区需要局部加密——两者共同指向 **在反应薄层做自适应加密 (AMR)** 作为下一步。
