# ARCH Studio — Linux / WSL desktop

ARCH Studio edits research configurations, builds selected cases and displays initial states and read-only Plotfiles. The scientific Core stays on Linux. Start with the [user guide](../docs/guides/Studio.zh-CN.md) ([English](../docs/guides/Studio.md)); implementation boundaries are in the [Core API](../src/api/README.md) and [Host Plotfile contract](docs/contracts/PLOTFILE_AUDIT_API.md).

## Build and launch

Core-only builds do not require Node or Electron. To build the desktop with the Core:

```bash
cmake --preset studio-cpu-release -DARCH_STUDIO_NODE=/absolute/path/to/node
cmake --build build-studio-cpu --parallel 2
build-studio-cpu/bin/arch-studio --project "$PWD" --binary build-studio-cpu/bin/ARCH --case Sod --config simulation/Sod/Sod.par
```

This generates a source-checkout launcher. It requires this checkout; it is not a portable distribution. The opt-in Studio target installs locked npm dependencies and builds production assets, including the Linux Electron runtime. Linux Node 24+ is required; Run/Restart also need `xterm` and `/usr/bin/flock`. WSL needs working WSLg. See [desktop lifecycle and packaging](desktop/README.md).

The development equivalent is `npm ci && npm run build` in this directory, then `desktop/arch-studio` with the same arguments. Put Node 24+ on PATH or set `ARCH_STUDIO_NODE` to its absolute executable. An explicit invalid Node path is an error. The desktop selects its internal loopback connections; users do not copy an IP/port or start Vite.

## Workflow and capability

The selected binary supplies the configuration schema, registered cases and Preview capabilities. Parameter edits may be previewed without Save; Run/Restart require an associated saved input and explicit confirmation. Changing C++ requires Configure/Build before its code can affect Preview or Run. Incomplete build-dependency evidence stays unknown rather than being accepted as fresh.

Initial fields use the case's real Setup/Init and shared EOS conversion. Initial AMR is a separate bounded request, overlaid on matching fields. Native-coordinate slices support the dimensions advertised by each case. Registration alone does not enable Preview: new cases need complete input declarations and reviewed sampling/AMR capabilities. Custom units are reviewed evidence tied to source identity; automatic inference of arbitrary C++ expressions is not implemented.

The Plotfile workspace reads full published files. Its supported format currently covers Sod 1D and Cartesian CellularDet 2D active leaves, bounded display queries and exact stored-cell inspection. It does not certify physical accuracy or support all 3D/curvilinear Plotfile layouts. XDMF, a reusable spatial index and bounded cross-query cache remain release-plan work.

## Source and evidence layout

| Location | Purpose |
| --- | --- |
| `src/`, `host/`, `desktop/` | Frontend, local service and Linux desktop production sources |
| [tests/](tests/README.md) | Engineering tests, fixtures and standalone test tools |
| [docs/contracts/](docs/contracts/PLOTFILE_AUDIT_API.md) | Maintained Host interface documentation |
| [docs/archive/](docs/archive/README.md) | Historical Phase/QA/UAT records |
| [STATUS.md](STATUS.md) | Current evidence and release closure status |
| [validation/](../validation/README.md) | Scientific references and reproduction tools |

Current integration is on `compute/optim`. Read the [integration/release plan](../docs/development/ComputeStudioReleasePlan-20261006.zh-CN.md) before final release work. Public RZ/CUDA JENS gates and their incomplete scientific findings remain explicit; a passing engineering checkpoint is not a completed release.

Environment prerequisites and installation: [Linux/WSL setup](../docs/guides/StudioEnvironment.md).
