# Plot output routing

[PlotIO.cpp](PlotIO.cpp) collects configured analysis fields and delegates to the
shared [HDF5 writer](../hdf5/README.md). It consumes synchronized host-visible
state and EOS callbacks supplied by the driver.

Plot files support post-processing. Restart state belongs to [chk](../chk/README.md).
Output selection is separate from time advancement and mathematical models.

## Internal RZ angular candidate

Explicit AxisymmetricRz IO uses NativeGrid version candidate-axisymmetric-rz-2,
geometry revision 2 and state_semantics rz-m-phi-j-over-w-v1. It does not enable
production RZ dispatch or the Cartesian-only Viewer.

- NativeGrid/cell_measure is V (cm^3, full_rotation).
- NativeGrid/angular_measure is W=int r*dV (cm^4, full_rotation), from
  GridMetrics::Rz::AngularMomentumMeasure; arrays follow interior Data cell order.
- NativeState/m_phi copies FluidState::mom_w without changing FP64 values:
  J_cell/W, g/(cm^2*s). NativeState/angular_momentum_density is derived J_cell/V,
  g/(cm*s), using the shared state conversion. Neither adds an evolved array.
- NativeState datasets have the Data shape [block,x2,x1], x1-fastest;
  NativeGrid measure vectors flatten the same order, excluding ghosts.
- VELZ is representative m_phi/rho, not a volume average of subcell azimuthal
  velocity. DENS/ENER retain native-volume-average semantics; EOS P/T and JENS
  are evaluations from the representative conserved state, not field averages.
- Missing, nonfinite, nonpositive W, wrong extents or inconsistent J/V reject
  before publication. Existing Cartesian field arrays/layout remain unchanged;
  the optional averaging attribute is unknown unless declared by the producer.

Raw H5 stays local. This candidate needs Core review; it is not a restart file,
a new frozen scientific reference, or evidence of complete RZ/CUDA acceptance.
