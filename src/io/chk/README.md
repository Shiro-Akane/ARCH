# Checkpoint and restart

[ChkIO.cpp](ChkIO.cpp) reads and writes the shared checkpoint payload.
[CheckpointCompatibility.h](CheckpointCompatibility.h) and its implementation
check scientific identities, species and format compatibility.

The reader and writer share the ARCH checkpoint contract. Identity, ENUC, native
species fractions and controller state are required; do not synthesize missing
restart fields. New-run initialization remains in `RunState`, separate from
checkpoint restoration. See the [format reference](../../../docs/Reference.md#arch-checkpoint).

Format 7 also requires `boundary_identity`: physical face types, compiled case
and callback source digests, and scientific custom inputs. Absolute source paths
and backend selection are excluded. Older formats or changed/missing identities
are rejected explicitly; boundary budgets restart at process launch.

Actual serialization is delegated entirely to the adjacent [HDF5 writer](../hdf5/README.md). Note that native composition, conserved species, controller states, and output phases are strict restart contracts, not opportunities for backend-specific state reconstruction. Refer to [restart validation](../../../validation/restart/README.md) for the exact recovery and forward-continuation checks, bearing in mind that these possess distinct acceptance criteria.


## RZ state identity

The RZ path uses geometry_semantics_revision=2,
geometry_chart=axisymmetric-rz and mandatory
state_semantics=rz-m-phi-j-over-w-v1. Data/mom_w is the unique
m_phi=J_cell/W_cell, with W=integral(r dV), in g/(cm^2 s).
Its representative azimuthal velocity is m_phi/rho; J/V is derived
from m_phi*W/V. The remaining conserved slots use native volume averages.
No additional independently evolved angular array is serialized.

Revision 1 cannot identify this representation and is rejected rather than
converted. A missing/different tag is rejected before live hierarchy replacement.
Layout version 6 and the existing chart revision 1 compatibility are unchanged.
The production startup reader selects this identity from the same resolved
chart used for fresh initialization. Host prescribed self-gravity supports
dynamic-AMR restart through the shared transaction. Other gated consumers and
continuous physical accuracy retain their separate acceptance requirements.


The RZ path also requires NativeDomain version 1:
FP64 bounds [r_min,r_max,z_min,z_max] in cm, root_blocks [Nr,Nz],
cell_shape [BLOCK_NX,BLOCK_NY], and full_rotation normalization.
The writer binds the actual tree root, rejects a differing active config,
and the reader matches domain and per-axis shape before live reconstruction.
Stored W has unit cm^4; this identity check is not a geometric accuracy proof.
Older RZ files without this domain identity are rejected, not converted.
The existing chart's checkpoint compatibility is unchanged.
