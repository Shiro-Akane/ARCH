# 光滑流体重构

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

> CPU 状态：PCM、MUSCL 和 PPM 通过。CUDA：待完成。

`simulation/SmoothAdvection/` 对周期 entropy wave \(\rho=1+0.2\sin(2\pi x)\)、\(u=1\)、\(p=1\) 平流至 \(t=0.1\)。初始和位移后的参考均为精确有限体积单元平均值。固定 HLLC 和 SSPRK3，分别在 64、128 和 256 单元上运行 PCM、MUSCL-MC 与 PPM。

## 复现

基线于 2026-08-20 从工作树基准 `50323fa4cf35adf7bf4e5a711adba93acd5448db` 运行，使用 GCC 13.3.0、Release/OpenMP、WSL2 x86-64、`OMP_NUM_THREADS=2` 和 `compute_backend=cpu`。公共 Release flags 为 `-O3 -march=native -ffast-math -DNDEBUG`；solver-dispatch 翻译单元另加 `-O1 -fno-inline-functions-called-once`。这些记录是开发基线；变更提交后，应以最终 commit 替换该 base hash。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 2
export OMP_NUM_THREADS=2
for p in simulation/SmoothAdvection/*.par; do
  ./bin/ARCH SmoothAdvection "$p"
done
```

将最终 `DENS` 单元平均值与由 \(ut\) 平移的解析波比较。`metrics.csv` 保留 L1/L2/Linf、观测 L1 阶数和相对质量漂移。验收要求最后一对分辨率的 L1 阶数：PCM 至少 0.9、MUSCL 至少 1.8、PPM 至少 2.7，且质量漂移不超过 \(10^{-12}\)。

| 方法 | N=256 的 L1 | 最终 L1 阶数 | 最大质量漂移 | 结果 |
| --- | ---: | ---: | ---: | --- |
| PCM | 9.780e-4 | 0.994 | 1.23e-14 | 通过 |
| MUSCL-MC | 9.314e-6 | 2.040 | 1.22e-14 | 通过 |
| PPM | 9.731e-10 | 3.993 | 1.22e-14 | 通过 |

![流体收敛](../assets/hydro_convergence.svg)

PPM 在通过所选 EOS 重构压力并保留光滑极值后超过 2.7 验收阶数。接近四阶的结果只属于该光滑常压接触测试，不构成通用四阶声明。此状态不应触发正性或核素修复。

CUDA 必须使用相同九个参数文件，并报告相对解析解及 CPU 输出的场 L1/L2；目前没有记录 CUDA 结果。
