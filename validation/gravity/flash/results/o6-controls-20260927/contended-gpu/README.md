# CUDA timings excluded from the final performance baseline

The user confirmed that another heavy GPU workload was active during the initial
seven-case and Davis timing groups. All fields passed the original backend
checks, but these timings do not establish isolated-device performance.

The separate `repeat2d` group preceded confirmation of an idle measurement
window; it is a diagnostic only, even though it reported positive speedup.
No original samples have been discarded or overwritten. The final `*_idle*`
groups repeat the whole protocol after the user confirmed the other load ended,
using the identical executable, inputs and field budgets.
