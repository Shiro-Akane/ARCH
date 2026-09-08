# Restart input pairs

- [uninterrupted.par](uninterrupted.par) and [resumed.par](resumed.par): source and
  resumed periodic-advection runs.
- [burn_uninterrupted.par](burn_uninterrupted.par) and
  [burn_resumed.par](burn_resumed.par): the corresponding uniform burn pair.

[Restart validation](../README.md) describes same-backend and cross-backend
coverage, native state restoration and subsequent evolution. Its
[shared runner](../../../tools/validate_cuda_amr_restart.py) connects an actual
source checkpoint to the resumed run and records the effective parameters.

Strict restoration compares saved native state at the checkpoint boundary;
scientific endpoint comparisons assess later evolution. Keep those checks and
their budgets distinct, and use the recorded recipe rather than assuming a
checkpoint path in an example input already exists.
