# Linux/WSL native Sod Run evidence

The saved Sod input was opened through the native Linux picker, inspected and
confirmed in ARCH Studio, then launched once in an independent terminal.
The retained job succeeded with exit code 0 at step 280, time 0.2.
summary.json records exact saved/frozen input and binary identities, input changes,
checkpoint endpoint and local-only output inventory. Only out_dir and chk_dt
changed relative to simulation/Sod/Sod.par; no scientific parameter was altered.

This is a fixed-grid CPU Run workflow check, not an independent Sod accuracy test,
AMR/CUDA validation or performance measurement.
Full build dependency freshness remains unknown. The terminal remains held after
completion by design; the worker and Core have exited.

The record covers the completed Run. Restart, cancellation, Stop and native
window visibility require their own checks; see [platform coverage](../../../../docs/releases/PlatformSupport.md).
Raw HDF5, checkpoints and full logs remain in local directories listed in the
inventory. The source checkpoint identity is `7d9b95f9`.
