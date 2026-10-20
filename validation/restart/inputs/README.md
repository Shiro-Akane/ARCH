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

The smooth source/resumed pair explicitly preserves the previous effective
controls: hll_wave_speed=roe and min_eint=1e-10. Their source is release
commit 25adec4224497981a0c124a3485f786194975be4,
StandardParameters.h and GlobalDefs.h. This config-v3 migration does not
change the physical inputs, endpoint, or comparison budgets. Static inspection
does not validate checkpoint existence or continuation readiness.
