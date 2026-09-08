# Plot output routing

[PlotIO.cpp](PlotIO.cpp) collects configured analysis fields and delegates to the
shared [HDF5 writer](../hdf5/README.md). It consumes synchronized host-visible
state and EOS callbacks supplied by the driver.

Plot files serve analysis; restart state belongs to [chk](../chk/README.md).
Keep output selection separate from time advancement and mathematical models.
