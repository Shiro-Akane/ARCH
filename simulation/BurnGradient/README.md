# Spatial burn pulse

[BurnGradient.cpp](BurnGradient.cpp) registers `BurnGradient`. It initializes a
temperature pulse on a one-dimensional Cartesian domain with a selected network
composition. Setup requires burning to be enabled.

The canonical [burn-AMR input](../../validation/amr/inputs/burn_enuc_amr.par)
uses this problem to exercise burn-energy refinement indicators. The
[AMR](../../validation/amr/README.md) and
[restart](../../validation/restart/README.md) summaries describe migration,
native burn-state restore and sustained continuation checks.

The initializer contains no AMR or ODE implementation; those operations remain
in the common driver and numerical modules.
