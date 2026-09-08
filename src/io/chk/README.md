# Checkpoint and restart

[ChkIO.cpp](ChkIO.cpp) reads and writes the shared checkpoint payload.
[CheckpointCompatibility.h](CheckpointCompatibility.h) and its implementation
check scientific identities, species and format compatibility.

The reader and writer share the ARCH checkpoint contract. Identity, ENUC, native
species fractions and controller state are required; do not synthesize missing
restart fields. New-run initialization remains in `RunState`, separate from
checkpoint restoration. See the [format reference](../../../docs/Reference.md#arch-checkpoint).

Actual serialization is delegated entirely to the adjacent [HDF5 writer](../hdf5/README.md). Note that native composition, conserved species, controller states, and output phases are strict restart contracts, not opportunities for backend-specific state reconstruction. Refer to [restart validation](../../../validation/restart/README.md) for the exact recovery and forward-continuation checks, bearing in mind that these possess distinct acceptance criteria.
