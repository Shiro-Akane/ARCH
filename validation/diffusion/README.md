# RKL1/RKL2 species diffusion mode

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

Diffusion smooths differences in composition between neighboring cells. Here a
cosine-shaped concentration profile has a known decay rate, so its amplitude
tests diffusion accuracy while total species mass tests conservation. Comparing
resolutions shows how the spatial error decreases.

The results on this page belong to the scientific acceptance snapshot identified
in the [central Validation index](../README.md). Source organization and build
verification have a separate
[maintenance record](../backend/results/maintenance-freeze-20260908/).

CPU and CUDA pass the same analytic-error, boundedness and conservation checks.

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

The [application evidence](../backend/results/uniform-native-20260907/release-874/backend-validation-evidence.json)
records both backends at the prescribed physical end time. Source, artifact and
build identities are recorded with the actual inputs. All six diffusion cases
pass on the tested build. Curved, dynamically refined diffusion and
hydrodynamic coupling are covered separately by the [AMR suite](../amr/README.md).

```bash
export OMP_NUM_THREADS=4
python3 tools/validate_backend_results.py \
  --manifest validation/backend/cases.json \
  --arch build-cuda/bin/ARCH \
  --checkpoint-validator build-cuda/arch_cuda_single_level_validation \
  --build-dir build-cuda --source-root . \
  --output-root validation/backend/results/uniform-new
```

RKL2 acceptance requires a final-pair tracer L1 rate of at least 1.8 and mean
tracer drift at most \(10^{-12}\). RKL1 acceptance requires finite bounded
fractions, Linf error at most \(10^{-5}\), and the same drift bound. Results are
measured from the final HDF5 cell averages.

The values below represent both backends at the displayed precision;
[metrics.csv](metrics.csv) retains their separate full-precision observations.
The acceptance run reproduces these values. At the physical endpoint,
the largest CPU/CUDA field difference is \(9.992\times10^{-16}\), within the
unchanged relative/absolute comparison budget of \(5\times10^{-10}\) and
\(2\times10^{-12}\).

| Integrator | Cells | L1 | L2 | L1 rate | Mean drift |
| --- | ---: | ---: | ---: | ---: | ---: |
| RKL1 | 64 | 1.714e-6 | 1.903e-6 | — | 0 |
| RKL1 | 128 | 3.567e-7 | 3.962e-7 | 2.265 | 0 |
| RKL1 | 256 | 2.221e-7 | 2.467e-7 | 0.684 | 0 |
| RKL2 | 64 | 4.851e-6 | 5.386e-6 | — | ≤1.12e-16 |
| RKL2 | 128 | 1.213e-6 | 1.347e-6 | 2.000 | ≤1.12e-16 |
| RKL2 | 256 | 3.032e-7 | 3.368e-7 | 2.000 | ≤1.12e-16 |

![Diffusion convergence](figures/convergence.svg)

Both backends pass. The RKL2 series is consistent with second-order spatial
convergence. The mixed resolution/stage-count RKL1 series is a stability,
boundedness, conservation, and analytic-error regression; it does not measure
RKL1 temporal order. CUDA also passes direct field comparisons with the CPU
on the same inputs.
