# Cooperative helium hotspots

[CooperativeHotspots.cpp](CooperativeHotspots.cpp) registers `CooperativeHotspots`
for a controlled two-dimensional helium-hotspot experiment.
[CooperativeHotspots.par](CooperativeHotspots.par) selects the initial hotspot
arrangement and thermodynamic conditions.

The existing [Chinese research notes](README.zh-CN.md) retain the study's
exploratory observations and proposed follow-up calculations. They describe
that research trajectory, not the project's combined release acceptance.

The generated initial states utilize the selected production EOS and network configuration. Common burn, hydro, and AMR behaviors are strictly documented and qualified via the [Validation](../../validation/README.md) module; you must not maintain any hotspot-specific solver logic here.
