# Species metadata and mixture access

[Species.h](Species.h) owns the species registry, names and thermodynamic/nuclear
metadata, along with the shared mixture queries.

SpeciesManager retains host-side registration. SpeciesHostView and
SpeciesPODView expose metadata through the same accessor contract; common
mixture functions provide the mathematical authority for both. CUDA owners
arrange device-accessible storage without defining another mixture model.

Reaction networks register their species, and EOS policies consume the resulting
metadata. This directory does not own reaction integration or table interpolation.

See the [Reference](../../../docs/Reference.md),
[EOS validation](../../../validation/eos/README.md) and
[network validation](../../../validation/network/README.md).
