# Plot output routing

[PlotIO.cpp](PlotIO.cpp) collects configured analysis fields and delegates to the
shared [HDF5 writer](../hdf5/README.md). It consumes synchronized host-visible
state and EOS callbacks supplied by the driver.

Plot files strictly serve post-processing analysis; in contrast, all restart state information belongs exclusively in [chk](../chk/README.md). You must keep output-selection logic entirely separate from any time advancement code and mathematical models.
