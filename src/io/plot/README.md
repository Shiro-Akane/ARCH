# Plot output routing

[PlotIO.cpp](PlotIO.cpp) collects configured analysis fields and delegates to the
shared [HDF5 writer](../hdf5/README.md). It consumes synchronized host-visible
state and EOS callbacks supplied by the driver.

Plot files support post-processing. Restart state belongs to [chk](../chk/README.md).
Output selection is separate from time advancement and mathematical models.

Native cylindrical 2D publishes the new `cylindrical-rz-pwc` chart with semantic
revision 1. Native axes are `(r,z,inactive)` in `(cm,cm,inactive)`; cell measure is
`cm^3` per unit azimuth. `VELX/VELY/VELZ` are the physical `(r,z,phi)` components,
so the third component contributes to kinetic energy despite having no spatial
phi derivative. Full-rotation integrals multiply these measures by `2*pi`.
Only formal native publications qualify this chart; old/empty/wrong identities
or axes/units are rejected. Studio reads the recorded bounds for r-z display,
Inspector and point queries; scientific qualification remains separate.
