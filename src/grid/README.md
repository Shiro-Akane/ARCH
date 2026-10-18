# Grid layout

[Grid.h](Grid.h) owns Host storage layout and topology. Shared physical
coordinates, volumes, areas and lengths live in the
[curvilinear numerical directory](../numerics/curvilinear/README.md).

CPU and CUDA borrow those same mathematical leaves. Device metric caching
lives in [cuda/common](../cuda/common/README.md); it defines no second geometry.

[GridGeometryView.h](../numerics/curvilinear/GridGeometryView.h) carries the
value-only coordinate layout and dyadic identity used by those consumers.
Density/inertia representation belongs to the numerical state owner, selected
traces to common reconstruction, and completed EOS acceptance to Runtime;
none of these responsibilities is Grid storage or topology.
