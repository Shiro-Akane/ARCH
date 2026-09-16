# Species metadata and mixture access

[Species.h](Species.h) owns the species registry, names and thermodynamic/nuclear
metadata, along with the shared mixture queries.

`SpeciesManager` manages all host-side species registration. `SpeciesHostView` and `SpeciesPODView` expose this metadata through an identical accessor contract; moreover, common mixture functions serve as the definitive mathematical authority for both. When executing on the device, CUDA owners arrange the necessary device-accessible storage without defining any alternative mixture model.

Reaction networks register their species, and EOS policies consume the resulting
metadata. This directory does not own reaction integration or table interpolation.

See the [Reference](../../../docs/Reference.md),
[EOS validation](../../../validation/eos/README.md) and
[network validation](../../../validation/network/README.md).
