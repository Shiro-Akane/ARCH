# External-gravity input sets

[euler.par](euler.par), [rk2.par](rk2.par) and [rk3.par](rk3.par) select time
integrators for the uniform constant-acceleration problem implemented by
[ExternalGravity](../../../simulation/ExternalGravity/README.md).

[Gravity validation](../README.md) owns analytic momentum/energy comparisons and
budgets. The [uniform backend matrix](../../backend/cases.json) and separate
[coupled cases](../coupled_cases.json) describe their actual coverage; these
inputs do not define a self-gravity test.

Use the recorded recipe to reproduce an execution and retain any applied
overrides with the input identity.
