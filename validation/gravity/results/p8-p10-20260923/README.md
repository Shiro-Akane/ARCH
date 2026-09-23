# P8–P10 1D radial self-gravity acceptance (2026-09-23)

Scope: CPU, native 1D spherical/cylindrical symmetry, `gravity_type=self`, isolated gravity boundary, composite AMR. The test executable was a Release CPU build of the `physics/selfgravity` working tree based on `6c0cb929`. The full radial run used the existing `/home/shiroakane/yt_env/bin/python` with NumPy and h5py and `OMP_NUM_THREADS=1`. Outputs were produced under `/tmp/arch-p10-evidence-final`; this directory preserves the compact [27-case summary](radial-summary.json) and [12-case elliptic output](elliptic-radial.txt).

The 1D elliptic test covers uniform and mixed AMR at 16/32/64 root cells in both geometries. All 12 cases passed: potential convergence order at least 1.8, independent analytic face Gauss error below `1e-9`, physical mass integral, and actual Poisson residual below its requested target. The finest cylindrical mixed case required 38 iterations. Its initial `1e-13` relative request fell below the measured FP64 physical-residual floor (about `2.7e-12` versus a `1.38e-12` target); with the user's explicit approval, this new test requests `3e-13`. No potential, Gauss, mass, or solver residual assertion was removed.

The 27-case full campaign passed uniform-density analytic sphere/cylinder fields at three resolutions, an origin-to-boundary Gauss-law integral for every plot, 12-step strong-gravity mixed-AMR evolution and bitwise checkpoint continuation, 40-step refine/coarsen/unchanged topology cycles, three-resolution short-time hydrostatic references, near-vacuum `rho0=1e-12` runs, and invalid-configuration rejections. The maximum radial Gauss relative error was `5.68e-9`. Strong-gravity 12-step closed-domain relative mass drift was at most `6.85e-16`; gas plus gravitational energy drift was `1.21e-5` (sphere) and `2.57e-5` (cylinder), below the frozen `5e-5` short-run budget. Restart datasets matched bitwise. Both weak-gravity regrid cycles retained relative mass and energy within `3e-15`. Hydrostatic parasitic velocity decreased over 16/32/64 cells at the same physical time in both geometries, but this is not a long-time well-balanced claim. The low-density cases completed two steps without repairs.

The existing quick self-gravity campaign passed 32 records. `arch_self_gravity`, `arch_gravity_stage_contract`, `arch_low_density`, the Cartesian standalone P2 solver, and the affected registered CPU tests passed. A full CPU target build succeeded. Narrow CUDA compilation of gravity execution/control and Hydro Ideal translation units succeeded; CUDA radial runtime, physics parity, and performance belong to P13 and are not claimed here. Reworking the small-cell time-step limit near the origin is also outside this milestone.

Reproduce from a CPU build configured with a Python environment containing NumPy and h5py:

```sh
cmake --build build-ci/cpu --target ARCH arch_composite_poisson -j 16
./build-ci/cpu/arch_composite_poisson radial
python3 validation/gravity/run_self_gravity.py --arch build-ci/cpu/bin/ARCH --output /tmp/arch-p8-p10-quick --quick
PYTHONPATH=validation/gravity python3 -c 'from radial_1d import RadialCampaign; RadialCampaign("build-ci/cpu/bin/ARCH", "/tmp/arch-p8-p10-full").run_checks(quick=False)'
```
