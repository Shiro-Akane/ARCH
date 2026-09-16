# Grid geometry

[Grid.h](Grid.h) owns host grid layout. [GridMetrics.h](GridMetrics.h) is the
authority for physical volumes, areas and lengths; [GridGeometryView.h](GridGeometryView.h)
provides a lightweight geometry view for numerical consumers.

These rigorous geometric conventions are shared universally across hydro, diffusion, CFL calculations, AMR, and diagnostics. The CUDA backend caches these exact metrics through [cuda/common](../cuda/common/README.md), and critically does not define any secondary coordinate system or alternate set of formulas. Comprehensive geometry checks are indexed in [AMR validation](../../validation/amr/README.md).
