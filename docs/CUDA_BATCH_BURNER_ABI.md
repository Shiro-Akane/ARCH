# CUDA batch-burner boundary

## Purpose

The burn backend should receive one explicit device-resident batch per source
stage.  It must not instantiate the whole Driver on the device or pass host
framework objects through a kernel boundary.  `BurnBatchInputSoA` and
`BurnBatchOutputSoA` are the minimal ABI between a future non-template Driver
adapter and network/EOS-specific CUDA launchers.

The present `launch_aprox13_fixed_cv_validation_batch` implementation is a
compileable integration skeleton around the already validated current aprox13
BE/Newton/dense-LU device mathematics.  Its name, header, and status make its
scope explicit: fixed `cv` is only a validation thermodynamic closure.  It is
not registered as a production launcher and cannot stand in for Helmholtz or
NSE.

## Device views

All scalar inputs are contiguous device arrays with one entry per cell:

- `density[cell]`
- `temperature[cell]`
- `dt_target[cell]`
- `heat_capacity_cv[cell]` in the validation implementation

Species use a species-major SoA layout:

```text
mass_fractions[species * species_stride + cell]
```

Outputs use the same layout plus one `status[cell]` and
`dt_recommended[cell]`.  Input and output may alias exactly because a cell is
loaded before commit; partial overlap is invalid.  A failed cell is
transactional and returns its original composition and temperature.

The views are standard-layout, trivially copyable PODs.  They contain no
`FluidState`, `Grid`, HDF5 object, host virtual interface, `std::function`, or
owning C++ container.  The public header also hides `cudaStream_t` behind an
opaque pointer, so a CPU-only translation unit can describe the ABI without
including CUDA headers.

## Intended Driver call sequence

For each Strang half-stage or unsplit burn stage:

1. The host Driver selects active cells and obtains persistent device SoA
   pointers from the simulation-state owner.
2. It constructs one input view, one output view, and one plain control block.
3. It calls exactly one network/EOS-specific launcher for the whole batch on
   the simulation stream.
4. Subsequent device work consumes successful cells without a mandatory host
   round trip.  Status is reduced on device; only the aggregate failure count
   and recommended global timestep need return to the host.
5. Any CPU retry is explicit, counted, and included in timing.  There is no
   silent backend fallback.

The launcher performs no allocation, packing, H2D/D2H copy, or synchronization.
Memory ownership and stream ordering remain outside the physics kernel.

## Status and timestep semantics

`BurnCellStatus == 0` means success.  Other bits distinguish invalid input,
singular matrices, non-finite arithmetic, inadmissible Newton states, exhausted
iterations, energy-closure failure, and unsupported physics.  This prevents an
empty or partially updated composition from being mistaken for a successful
burn.

For the fixed-substep validation launcher, `dt_recommended` is the successful
fixed substep.  A numerical failure recommends one quarter of that step; an
invalid input returns zero.  This is deliberately not presented as the
production PI controller.  The production launcher must match the CPU ODE
controller and report its actual accepted/recommended step.

## Replacement of fixed cv

The production boundary should keep the same SoA state views but replace the
fixed-cv kernel specialization with an explicitly instantiated concrete EOS
device view, for example:

```cpp
template <typename Net, typename DeviceEos, typename OdePolicy>
void burn_batch_kernel(BurnBatchInputSoA, BurnBatchOutputSoA,
                       DeviceEos eos, OdePolicy controls);
```

`DeviceEos` must be a small trivially copyable view of device tables/constants;
it must not use host virtual dispatch.  The production implementation must call
Helmholtz consistently for `cv`, internal energy, temperature inversion, and
the energy-closure test.  High-temperature cells must enter the real generic
NSE path or return `BurnCellUnsupportedPhysics`; freezing composition is not an
allowed shortcut.

## Current validation launcher limitations

- aprox13 only;
- one thread per cell, fixed positive `cv`;
- predetermined validation substeps rather than production adaptive stepping;
- dense 14-by-14 LU local to a thread;
- no Helmholtz table/device view;
- no NSE;
- no production runtime registration;
- no claim of Cellular, reactive-hydro, or end-to-end speedup.

The separate `cuda_aprox13_be_validation` test remains the CPU/CUDA
mathematical parity gate.  `cuda_aprox13_batch_api_validation` exercises the
new SoA layout, asynchronous stream launch, explicit status, transactional
failure, `dt_recommended`, stride padding, and host-side view rejection.

On the JAIST NVIDIA H100-20C, CUDA 12.4/sm90, the ABI test was built without a
CMake change:

```bash
nvcc -std=c++20 -O2 --fmad=false --expt-relaxed-constexpr -arch=sm_90 \
  -I src tools/cuda_aprox13_batch_api_validation.cu \
  src/cuda/Aprox13FixedCvValidationBatch.cu \
  -o /tmp/cuda_aprox13_batch_api_validation
```

The eight-state SoA test passed status, transactional failure, timestep
recommendation, stride-padding, and finite-value checks with a maximum
`sum(X)-1` error of `3.330669073875e-16`.  The independent existing bounded BE
correctness executable also passed on the same H100.  `compute-sanitizer` was
not installed on the node, so sanitizer coverage is not claimed.
