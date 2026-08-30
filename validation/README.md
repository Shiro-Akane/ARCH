# ARCH Verification and Validation

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

This directory is the single entry point for quantitative verification records.
The CPU baselines cover uniform-grid hydro reconstruction, RKL1/RKL2 species
diffusion, constant external gravity, an aprox13 one-zone burn, AMR transfer and
conservation, tabular-EOS interpolation, restart continuity, and generated
custom networks with KLU. CUDA rows are reserved until the V2 backend can run
the same committed inputs.

## Directory contract

Each module owns one subdirectory containing its README, machine-readable metrics,
immutable baseline parameter files under `inputs/`, and optional `figures/`. Each
validation `.par` file names its owning record and must change together with the
relevant metrics and acceptance decision. Reusable teaching and example inputs
remain under `simulation/`; runtime data remain under `EOS_toolkit/`. Do not create
a parallel validation tree.

## Current status

| Area | CPU result | CUDA result | Record |
| --- | --- | --- | --- |
| Smooth hydro reconstruction | PCM, MUSCL, and PPM pass | pending | [hydro](hydro/README.md) |
| RKL1/RKL2 species diffusion | both pass; RKL2 shows second-order spatial convergence | pending | [diffusion](diffusion/README.md) |
| External gravity | pass for RK2/RK3 constant-acceleration update | pending | [gravity](gravity/README.md) |
| aprox13 one-zone burn | BD and ROS4 pass the BE_NR comparison | pending | [burn](burn/README.md) |
| AMR | transfer/reflux conservation and basic 2D symmetry pass; local refinement retention is a known limitation | pending | [AMR](amr/README.md) |
| Tabular EOS | normalized 3D/4D smooth sweep passes; Shen assets assessed, not accepted | pending | [EOS](eos/README.md) |
| HDF5/restart | v1 compatibility and v2 hydro/burn continuity pass; dynamic-AMR split run pending | pending | [restart](restart/README.md) |
| Generated networks/KLU | multi-size generation, coexistence, sparse solve, and one-step burn pass | pending | [network](network/README.md) |
| Network-constrained NSE | existing CPU/thread evidence retained in the Timmes network record | pending | [Timmes networks](../docs/physics/TimmesNetworks.md) |
| Sod analytic solution and manufactured geometry | pending | pending | planned |

“Pending” is an explicit placeholder, not evidence of backend parity.

## Error conventions

Verification compares the implementation with analytic, manufactured, or
independently converged references. Validation against experimental or
published physical data will be labeled separately.

For cell-volume-weighted field error,

\[
L_1(q)=\frac{\sum_i V_i\lvert q_i-q_i^{ref}\rvert}{\sum_i V_i},
\qquad
L_2(q)=\sqrt{\frac{\sum_i V_i(q_i-q_i^{ref})^2}{\sum_i V_i}}.
\]

The observed rate between resolutions \(N\) and \(2N\) is
\(p=\log_2(E_N/E_{2N})\). Each record states any different norm or
normalization. Machine-readable results are retained as CSV; a committed data
processing script is optional when the formula, sampled outputs, commands, and
metrics are sufficient to reproduce the decision.

## Record requirements

Each completed record states:

1. the property and modules tested, equations, dimension, and geometry;
2. immutable `.par` inputs and checksums for external EOS/reference assets;
3. configure, build, and run commands;
4. commit, compiler/flags, OpenMP count, backend, and relevant hardware;
5. reference provenance and sampling rules;
6. L1/L2 errors or the module-specific residuals and invariants;
7. an acceptance tolerance and explicit pass/fail result;
8. retained CSV metrics and, where useful, a static figure;
9. any safeguard activation or known implementation limitation.

AMR comparisons must place reference and numerical fields on a documented
common mesh and use physical cell volumes. CPU/CUDA comparisons will use the
same case source, parameters, reference, and metric definitions.

## Remaining matrix

| Area | Planned reference | Required measurements |
| --- | --- | --- |
| Hydro/Riemann | analytic 1D Sod solution | density, velocity, pressure, energy L1/L2; feature positions; conservation |
| Strong shock | Sedov similarity solution | radial-profile L1/L2, shock radius, energy, symmetry |
| Hydro time integration | smooth semi-discrete reference | error versus time step for Euler, SSPRK2, SSPRK3 |
| Geometry | cylindrical/spherical manufactured solution | volume-weighted L1/L2 and source balance |
| AMR follow-up | stable local interface crossing after ghost-transfer repair | common-mesh L1/L2, retained local topology, curved-coordinate face consistency |
| EOS follow-up | family-specific nuclear-matter converter | native-field transforms, phase masks, axis-halving, trajectory checks |
| NSE | independent equilibrium states | composition/thermodynamic L1/L2 and equilibrium residuals |
| HDF5/restart follow-up | dynamic-AMR uninterrupted run | topology and metadata parity, including refinement diagnostics |
| CPU/CUDA | identical records above | field/state L1/L2, invariants, configuration and device metadata |

Use [CASE_TEMPLATE.md](CASE_TEMPLATE.md) for new records.
