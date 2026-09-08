# Cooperative helium hotspots

[CooperativeHotspots.cpp](CooperativeHotspots.cpp) registers `CooperativeHotspots`
for a controlled two-dimensional helium-hotspot experiment.
[CooperativeHotspots.par](CooperativeHotspots.par) selects the initial hotspot
arrangement and thermodynamic conditions.

The existing [Chinese research notes](README.zh-CN.md) retain the study's
exploratory observations and proposed follow-up calculations. They describe
that research trajectory, not the project's combined release acceptance.

The initial states use the selected production EOS and network. Common burn,
hydro and AMR behavior is documented and qualified through
[Validation](../../validation/README.md); no hotspot-specific solver is maintained here.
