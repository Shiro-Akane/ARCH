# Smooth hydro reconstruction

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

> CPU status: PCM, MUSCL, and PPM pass. CUDA: pending.

The `SmoothAdvection` implementation remains in `simulation/SmoothAdvection/`;
the immutable parameter files owned by this record are in [`inputs/`](inputs/).
They advect a periodic entropy wave with
\(\rho=1+0.2\sin(2\pi x)\), \(u=1\), and \(p=1\) to \(t=0.1\). The initial and
translated references are exact finite-volume cell averages. HLLC and SSPRK3
are fixed while PCM, MUSCL-MC, and PPM are run at 64, 128, and 256 cells.

## Reproduce

The baseline was run on 2026-08-20 from working tree base
`50323fa4cf35adf7bf4e5a711adba93acd5448db`, GCC 13.3.0, Release/OpenMP, WSL2
x86-64, with `OMP_NUM_THREADS=2` and `compute_backend=cpu`. Effective common
Release flags are `-O3 -march=native -ffast-math -DNDEBUG`; solver-dispatch
translation units add `-O1 -fno-inline-functions-called-once`. The records are
a development baseline; replace the base hash with the final commit when these
changes are committed.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 2
export OMP_NUM_THREADS=2
for p in validation/hydro/inputs/*.par; do
  ./bin/ARCH SmoothAdvection "$p"
done
```

The final `DENS` cell averages are compared with the analytic wave translated
by \(ut\). `metrics.csv` retains L1/L2/Linf, observed L1 rate, and relative mass
drift. Acceptance is final-pair L1 rate at least 0.9 for PCM, 1.8 for MUSCL, and
2.7 for PPM, with mass drift at most \(10^{-12}\).

| Method | L1 at N=256 | Final L1 rate | Max mass drift | Result |
| --- | ---: | ---: | ---: | --- |
| PCM | 9.780e-4 | 0.994 | 1.23e-14 | pass |
| MUSCL-MC | 9.314e-6 | 2.040 | 1.22e-14 | pass |
| PPM | 9.731e-10 | 3.993 | 1.22e-14 | pass |

![Hydro convergence](figures/convergence.svg)

The PPM series exceeds its 2.7 acceptance rate after reconstructing pressure
through the selected EOS and retaining smooth extrema. The near-fourth-order
rate belongs to this smooth constant-pressure contact test; it is not a general
fourth-order claim. No positivity or species repair is expected for this state.

CUDA must use these same nine parameter files and report field L1/L2 against
both the analytic solution and CPU output; no CUDA result is recorded yet.
