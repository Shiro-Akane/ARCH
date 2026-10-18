# Cooperative helium hotspots

[CooperativeHotspots.cpp](CooperativeHotspots.cpp) registers `CooperativeHotspots`
for a controlled two-dimensional helium-hotspot experiment.
[CooperativeHotspots.par](CooperativeHotspots.par) selects the initial hotspot
arrangement and thermodynamic conditions.

The [current CPU reliability record](../../validation/backend/results/release-readiness/REL3CpuScienceAndReliability-20261014.json)
checks all four arrangements through the public 4.1 ns endpoint, including an
independent cell-center geometry check, finite positive states, composition
closure and zero repairs. Ending density bisection when its midpoint stagnates
at an endpoint preserves the single-hotspot initial and final physical arrays
bit for bit (49 steps). The original iteration cap, final pressure check and
source verification on each actual EOS query remain. One measured run pair
changes 20.35 to 14.26 seconds; this is a local observation, not a general
performance guarantee or an independent validation of the proposed ignition
mechanism. The receipt separates the rebuilt application from prior evidence.

The [CPU continuation record](../../validation/backend/results/release-readiness/REL3CpuScienceFollowup-20261014.json)
also checks the single-hotspot case from both checkpoint phases through 4.1 ns.
Native physical arrays match bit for bit, with the original controller and
actual output-history gates intact. Different output schedules are not added
to the performance comparison above; ignition science remains a separate claim.

The existing [Chinese research notes](README.zh-CN.md) retain the study's
exploratory observations and proposed follow-up calculations. They describe
that research trajectory, not the project's combined release acceptance.

The generated initial states utilize the selected production EOS and network configuration. Common burn, hydro, and AMR behaviors are strictly documented and qualified via the [Validation](../../validation/README.md) module; you must not maintain any hotspot-specific solver logic here.
