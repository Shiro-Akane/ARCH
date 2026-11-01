# Studio illustrations

The [Studio guide](../Studio.md) uses the two-dimensional Sedov case to show a
complete workflow: model and parameter selection, initial pressure, parameter
inspection, Run preparation and an evolved density Plotfile.

To reproduce the views, build Core and Studio using the [build guide](../Build.md),
copy `simulation/Sedov/Sedov.par` to an experiment input and choose a fresh output
directory. The output illustration uses `DENS` at `t = 0.04 s`, with 280 leaf
blocks of 16 × 16 cells. The display samples the full field onto a 32 × 24 grid;
the enabled outline overlay shows its bounded subset of 128 blocks. Read raw
samples or select a cell to inspect stored values.

The parameter illustration selects `deposit_radius = 0.03 cm` and displays the
initial pressure field. The Run illustration shows the saved input, requested
and selected compute backends, and linear solver before launch confirmation. The images show the shared Linux/Windows workspaces in a Linux
Electron window.

| Image | View |
| --- | --- |
| `studio-config.png` | Config layout, model/source association and initial pressure |
| `studio-parameters.png` | Parameter Help and Inspector |
| `studio-run.png` | Run preparation and confirmation |
| `studio-plotfile.png` | Project file selection and output metadata |
| `studio-overview.png` | Evolved density and AMR leaf outlines |
