# Physical boundary services

[BoundaryTypes.h](BoundaryTypes.h) defines distinct physical and potential
contexts/results, using the existing axis/side types and coordinate mapping.
[UserBoundary.h](UserBoundary.h) resolves compiled same-directory registrations
and builds portable restart identity.

[PhysicalBoundary.h](PhysicalBoundary.h) owns EOS-backed primitive validation
and scalar ghost rules. [PhysicalBoundaryHandler](PhysicalBoundaryHandler.h)
traverses only domain faces, inheriting builtin ghosts before evaluating user
requests. It uses the same evaluator on CPU and CUDA boundary snapshots.

[BoundaryFlux.h](BoundaryFlux.h) is the lightweight shared transport control
leaf. [BoundaryDiagnostics.h](BoundaryDiagnostics.h) observes actual face fluxes;
stage scheduling, metric integration and IO remain in the Driver.
Potential conditions feed the existing `gravity/self` and composite Poisson
owners, rather than a second elliptic solver.

The [user guide](../../../docs/guides/UserBoundaries.md) specifies filenames,
CGS signs, stage times, regularity joins, threading and restart. Ordinary
builtin paths do not configure callback surface storage or observers.
