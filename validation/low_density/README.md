# Low-density state validation

This P1.5 suite tests finite, positive-density IdealGas states. It does not
certify exact vacuum, cancellation-dominated thermal energy or extrapolation
outside a table EOS domain. The implementation and current acceptance record
are in [P1.5](../../docs/development/P1_5ImplementationReport.zh-CN.md).

`manifest.json` fixes density scales and scientific budgets. `run.py` executes
72 actual ARCH runs, reads their checkpoints and checks analytic entropy-wave
convergence, symmetric rarefaction, constant acceleration, physical-volume
conservation, active floor accounting, restart, evolving AMR, density contrast,
five flux schemes, three active-floor budgets with a fixed observation region, inactive-floor sensitivity and RKL1/RKL2 species diffusion.
References use analytic equations; CPU/CUDA agreement is supplementary.

```sh
python3 validation/low_density/run.py --arch bin/ARCH \
  --backend cpu --output-root /tmp/arch-low-density-cpu
python3 validation/low_density/run.py --arch build/cuda/bin/ARCH \
  --backend cuda --output-root /tmp/arch-low-density-cuda
```

Use the actual path of your CUDA-enabled executable in the second command.
Both commands require Python with NumPy and h5py. Each output directory must
be empty or new. A successful run writes `results.json` and `PASS`; failures
retain case inputs/logs but cannot retain a stale success marker.

`low_density_math` and `hydro_leaf_parity` additionally test shared
Host/Device mathematical leaves through `rho=1e-100`, including independent
Euler flux, gravity, CFL and linear-system answers. Table EOS tests separately
check their declared thermodynamic domains, unique inverses and invalid queries.
The full CTest suite remains part of acceptance.

Per-case wall times include startup and I/O. The small cases are correctness
witnesses, not a GPU throughput benchmark. New result packages retain compact
metrics and logs here; generated HDF5 files stay outside Git.
