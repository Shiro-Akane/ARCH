# Simulation problems and example inputs

Each directory owns a problem's initialization and any reusable example `.par`
files. Problems register a runtime name and hand their initial state to the
common driver; backend selection and physics algorithms remain outside the
problem definition.

Start with the [simulation guide](../docs/guides/SimulationCase.md)
([Chinese](../docs/guides/SimulationCase.zh-CN.md)) and the small
[Sod input](Sod/Sod_beginner.par). A directory name need not equal its registered
runtime name; the catalogue below gives the actual names.

| Directory | Runtime name | Purpose |
|---|---|---|
| [Sod](Sod/README.md) | `Sod` | One-dimensional shock tube and introductory input |
| [Sedov](Sedov/README.md) | `Sedov` | Finite-radius Cartesian blast and AMR evolution |
| [SmoothAdvection](SmoothAdvection/README.md) | `SmoothAdvection` | Periodic entropy-wave transport |
| [GaussianPulse](GaussianPulse/README.md) | `Gaussian` | Species pulse with optional pressure and velocity perturbations |
| [DiffusionMode](DiffusionMode/README.md) | `DiffusionMode` | Periodic cosine mode for species diffusion |
| [ExternalGravity](ExternalGravity/README.md) | `ExternalGravity` | Uniform state under constant external acceleration |
| [BurnOneZone](BurnOneZone/README.md) | `BurnOneZone` | Uniform network evolution through the production burn driver |
| [BurnGradient](BurnGradient/README.md) | `BurnGradient` | Spatial burn pulse for energy-driven AMR and restart |
| [RTinstability](RTinstability/README.md) | `RT` | Rayleigh–Taylor stratification and mixing |
| [Cellular](Cellular/README.md) | `CellularDet` | Reactive shock and cellular-detonation initial conditions |
| [CooperativeHotspots](CooperativeHotspots/README.md) | `CooperativeHotspots` | Controlled helium-hotspot research experiment |

Canonical validation inputs, reference methods and budgets live under
[validation/](../validation/README.md), even when they reuse these initializers.
Several validation-focused problems therefore have no duplicate `.par` file
here. Research notes and example inputs are separate from qualified scientific
results; follow each problem's links for its intended use.

To add a problem, follow the guide's registration workflow and add it to this
catalogue. Keep reusable initial conditions here, simulation output under the
chosen output directory, and run-specific evidence in the owning Validation module.
