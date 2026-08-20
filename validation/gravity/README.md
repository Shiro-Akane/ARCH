# Constant external gravity

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

> CPU status: pass for RK2/RK3. CUDA: pending.

`simulation/ExternalGravity/` starts from a uniform periodic state with
\(\rho=1\), \(p=1\), \(u=0\), and constant \(g_x=1\). At \(t=0.1\), the exact
solution is \(u=g_xt=0.1\), unchanged density and pressure, and
\(E=p/(\gamma-1)+\rho u^2/2\). Spatial flux divergence is zero, so the case
isolates the gravity source inside the hydro time integrator.

## Reproduce

Environment and build provenance match the [hydro record](../hydro/README.md).

```bash
export OMP_NUM_THREADS=2
for p in simulation/ExternalGravity/*.par; do
  ./bin/ARCH ExternalGravity "$p"
done
```

Acceptance for RK2 and RK3 is Linf error at most \(10^{-12}\) in density,
velocity, pressure, and total energy. Euler is retained to expose its expected
first-order source-energy error, not used to claim second-order accuracy.
The state remains spatially uniform, so each listed Linf value is also its L1
and L2 value.

| Integrator | velocity Linf | pressure Linf | energy Linf | Result |
| --- | ---: | ---: | ---: | --- |
| Euler | 0 | 1.006e-4 | 2.515e-4 | diagnostic |
| RK2 | 0 | 2.220e-16 | 0 | pass |
| RK3 | 8.327e-17 | 4.441e-16 | 4.441e-16 | pass |

The result verifies the constant external-gravity source path on CPU. It is not
a hydrostatic-balance or self-gravity test. CUDA remains an empty parity row.
