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

Native cylindrical 2D checkpoints store PWC momenta `(rho*u_r,rho*u_z,rho*u_phi)`
and require `geometry_semantics_revision=1`, `geometry_chart=cylindrical-rz-pwc`.
The reader rejects retired `axisymmetric-rz`, empty, partial or wrong identities
before replacing live state. The eight retained charts keep their existing
identity and legacy-empty rules. CHK adds no source/build/profile requirement.
See the bounded [CPU fresh fixture](../../../validation/restart/inputs/cylindrical.par).
