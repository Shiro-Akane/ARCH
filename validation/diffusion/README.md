# RKL1/RKL2 species diffusion mode

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

> CPU status: pass. CUDA: pending.

The `DiffusionMode` implementation remains in `simulation/DiffusionMode/`; the
immutable parameter files owned by this record are in [`inputs/`](inputs/).
They evolve a bounded tracer mass fraction on a static,
periodic, one-dimensional ideal-gas state:

\[
X(x,t)=0.5+0.25\exp[-D(2\pi)^2t]\cos(2\pi x),\qquad D=0.01.
\]

The reference includes the exact cell-average sinc factor. Only species
diffusion is enabled; the background fraction is `1-tracer`. The committed RKL1
and RKL2 inputs use 64, 128, and 256 cells and end at \(t=0.1\).

## Reproduce

Environment and build provenance match the [hydro record](../hydro/README.md).

```bash
export OMP_NUM_THREADS=2
for p in validation/diffusion/inputs/*.par; do
  ./bin/ARCH DiffusionMode "$p"
done
```

RKL2 acceptance requires a final-pair tracer L1 rate of at least 1.8 and mean
tracer drift at most \(10^{-12}\). RKL1 acceptance requires finite bounded
fractions, Linf error at most \(10^{-5}\), and the same drift bound. Results are
measured from the final HDF5 cell averages.

| Integrator | Cells | L1 | L2 | L1 rate | Mean drift |
| --- | ---: | ---: | ---: | ---: | ---: |
| RKL1 | 64 | 1.714e-6 | 1.903e-6 | — | 0 |
| RKL1 | 128 | 3.567e-7 | 3.962e-7 | 2.265 | 0 |
| RKL1 | 256 | 2.221e-7 | 2.467e-7 | 0.684 | 1.11e-16 |
| RKL2 | 64 | 4.851e-6 | 5.386e-6 | — | 0 |
| RKL2 | 128 | 1.213e-6 | 1.347e-6 | 2.000 | 0 |
| RKL2 | 256 | 3.032e-7 | 3.368e-7 | 2.000 | 5.55e-17 |

![Diffusion convergence](figures/convergence.svg)

Both CPU paths pass. The RKL2 series is consistent with second-order spatial
convergence. The mixed resolution/stage-count RKL1 series is a stability,
boundedness, conservation, and analytic-error regression; it does not measure
RKL1 temporal order. CUDA parity is reserved against the same analytic
reference and committed inputs.
