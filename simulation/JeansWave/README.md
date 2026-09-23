# Stable Jeans wave (CGS)

Run `./build-cpu/bin/ARCH JeansWave simulation/JeansWave/JeansWave.par` from the repository root after using the `cpu-release` preset; use the matching executable for another build.
The default is a 1 cm periodic domain, background density `1e7 g/cm^3`, pressure
`6e6 erg/cm^3`, gamma `5/3`, physical CGS G and relative density amplitude `1e-4`.
The case initializes a right-moving stable linear Jeans mode along x. Small finite
amplitude introduces nonlinear error; this is a convergence example, not an exact
finite-amplitude nonlinear solution.

Case controls are `rho0`, `pressure0`, `amplitude`, integer `mode` and phase in
radians (`phase`). Global gravity controls remain in GravityConfig. The root-cell
sinc factor initializes volume averages. Initial AMR uses the shared conservative
hydro transfer; it does not re-evaluate an analytic state on every refined cell.

The continuum frequency satisfies `omega^2 = gamma*p0/rho0*k^2 - 4*pi*G*rho0`.
The example rejects unstable parameters. Plot output includes potential and
cell acceleration; inspect `gravity_solves.tsv` and `state_repairs.txt` alongside
fluid fields. This example exercises a periodic Jeans mode. For production
isolated boundaries and coupled CPU/CUDA inputs, see [GravityBox](../GravityBox/README.md)
and the [current acceptance](../../docs/development/P5P7GravityAcceptance.zh-CN.md).

Run the independent acceptance campaign with
`python3 validation/gravity/run_self_gravity.py --arch ./build-cpu/bin/ARCH --output /tmp/jeans-validation`
(requires numpy and h5py). It checks analytic wave error, time order, total energy,
mass, actual dynamic regrids and bitwise checkpoint recovery. The standalone
composite-operator test separately checks force convergence and nonsymmetric
AMR net-force error in three dimensions.
