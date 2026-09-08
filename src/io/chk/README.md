# Checkpoint and restart

[ChkIO.cpp](ChkIO.cpp) reads and writes the shared checkpoint payload.
[CheckpointCompatibility.h](CheckpointCompatibility.h) and its implementation
check scientific identities, species and format compatibility.

Serialization is implemented by the adjacent [HDF5 writer](../hdf5/README.md).
Native composition, conserved species, controller state and output phase are
restart contracts, not opportunities for backend-specific reconstruction.
See [restart validation](../../../validation/restart/README.md) for exact recovery
and forward-continuation checks, which have distinct acceptance criteria.
