# Grid geometry

[Grid.h](Grid.h) owns host grid layout. [GridMetrics.h](GridMetrics.h) is the
authority for physical volumes, areas and lengths; [GridGeometryView.h](GridGeometryView.h)
provides a lightweight geometry view for numerical consumers.

Hydro, diffusion, CFL, AMR and diagnostics share these conventions. CUDA caches
the metrics through [cuda/common](../cuda/common/README.md), without defining
another coordinate system or set of formulas. Geometry checks are indexed in
[AMR validation](../../validation/amr/README.md).
