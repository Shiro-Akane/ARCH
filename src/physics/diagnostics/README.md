# Velocity diagnostics

[VelocityDiagnostics.h](VelocityDiagnostics.h) owns the metric-aware divergence
and vorticity diagnostics used by refinement indicators and plot output.

Its shared evaluation accepts grid and velocity accessors. CPU and CUDA callers
provide storage access; the diagnostic operator and coordinate conventions stay
in this header. Geometric measures come from the common GridMetrics authority,
not from an output-specific or device-specific formula.

See the [Reference](../../../docs/Reference.md) for diagnostic field names and
[AMR validation](../../../validation/amr/README.md) for their integration checks.
