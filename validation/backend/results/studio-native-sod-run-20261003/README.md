# Linux/WSL native Sod Run evidence

The saved Sod input was opened through the native Linux picker, inspected and
confirmed in ARCH Studio, then launched once in an independent terminal.
The retained job succeeded with exit code 0 at step 280, time 0.2.
summary.json records exact saved/frozen input and binary identities, input changes,
checkpoint endpoint and local-only output inventory. Only out_dir and chk_dt
changed relative to simulation/Sod/Sod.par; no scientific parameter was altered.

This is a fixed-grid CPU Run workflow check, not an independent Sod accuracy test,
AMR/CUDA validation, performance measurement or full 3C acceptance.
Full build dependency freshness remains unknown. The terminal remains held after
completion by design; the worker and Core have exited.

A separate Restart input/checkpoint selection was prepared. Its pending native
confirmation was cancelled without starting Restart. It is not a passed test.
The latest agent capture shows the independent Linux window, but the user reports
only a taskbar icon; current user-visible desktop acceptance remains pending.
Do not replace that acceptance with a capturable window or browser view.

Raw H5, checkpoints and full logs remain in ignored local directories referenced
by the inventory. No raw arrays are included. No new Run was executed to prepare
this summary. The source checkpoint identity is 7d9b95f9; this evidence update does
not rebuild Core or claim a new binary.
