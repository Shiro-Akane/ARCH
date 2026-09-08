# Burn input sets

[be_nr_reference.par](be_nr_reference.par), [bd.par](bd.par) and
[ros4.par](ros4.par) initialize the same uniform aprox13/Helmholtz burn problem
with the respective ODE method. They use the production
[BurnOneZone](../../../simulation/BurnOneZone/README.md) initializer and driver.

[Burn validation](../README.md) owns the thermal reference, comparison intervals
and acceptance budgets; [time_reference.py](../time_reference.py) provides the
independent time-integration checks. A filename containing `reference` does not
make that ODE trajectory an independent scientific oracle.

Preserve the input identity with each result. Campaign-specific overrides belong
in the recorded execution recipe rather than unrecorded edits to these files.
