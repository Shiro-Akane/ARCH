# Plot output routing

[PlotIO.cpp](PlotIO.cpp) collects configured analysis fields and delegates to the
shared [HDF5 writer](../hdf5/README.md). It consumes synchronized host-visible
state and EOS callbacks supplied by the driver.

Plot files support post-processing. Restart state belongs to [chk](../chk/README.md).
Output selection is separate from time advancement and mathematical models.
