# Velocity diagnostics

[VelocityDiagnostics.h](VelocityDiagnostics.h) owns the metric-aware divergence
and vorticity diagnostics used by refinement indicators and plot output.

Its shared evaluation routine accepts generalized grid and velocity accessors. CPU and CUDA callers simply provide the necessary storage access; the actual diagnostic operator and coordinate conventions remain strictly within this header. Importantly, geometric measures are sourced directly from the common `GridMetrics` authority, rather than from any output-specific or device-specific formula.

See the [Reference](../../../docs/Reference.md) for diagnostic field names and
[AMR validation](../../../validation/amr/README.md) for their integration checks.
