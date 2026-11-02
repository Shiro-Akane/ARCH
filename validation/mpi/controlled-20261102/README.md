# Fixed-grid MPI controlled measurements

Run CPU Cartesian hydro across MPI ranks while preserving the existing numerical kernels, pool allocation and X-face exchange workspace. This campaign integrates upstream v1.4.1, verifies serial and distributed behavior, and separates communication costs from complete simulation time.

## Build and validate

Use the project's [build guide](../../../cmake/README.md) to provide the normal Core dependencies. Configure independent runtime directories for MPI-ON and MPI-OFF builds, for example:

~~~sh
cmake -S . -B build/mpi -DARCH_ENABLE_MPI=ON -DARCH_ENABLE_CUDA=OFF \
  -DARCH_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build/mpi/bin"
cmake --build build/mpi --target ARCH arch_mpi_fixed_grid -j 2
ctest --test-dir build/mpi --output-on-failure -R '^mpi_fixed_grid_[124]$'
~~~

For a serial control, configure another build directory with `ARCH_ENABLE_MPI=OFF` and a different `ARCH_RUNTIME_OUTPUT_DIRECTORY`. The archived integration run additionally rebuilds Debug and Release serial controls and checks the new execution preflight contracts.

## Measurement protocol

[protocol.json](protocol.json) fixes shapes, step counts, repeats, hosts and acceptance rules. `run.py` uses the existing [measurement parser](../scaling-20261101/measure.py), [rank wrapper](../scaling-20261101/rank_wrapper.py) and [integration comparator](../../../tests/integration/test_mpi_fixed_grid.py). `mpi_trace.c` is a separate preload observer, never part of the production binary.

Use the preparation command with already-built executables and their actual source revisions. It creates a fresh evidence directory, copies only the required helpers and inputs, compiles the PMPI observer, and records binary hashes:

~~~sh
python -B validation/mpi/controlled-20261102/prepare.py \
  --binary /server/candidate/build/bin/ARCH --candidate-revision <candidate-sha> \
  --baseline /server/reference/build/bin/ARCH --baseline-revision <reference-sha> \
  --evidence /server/evidence --toolchain /server/toolchain
~~~

The two-host addresses in the runner correspond to the recorded servers; adapt these explicit locations when reproducing elsewhere.

~~~sh
python -B validation/mpi/controlled-20261102/run.py \
  --evidence /server/evidence --work /dev/shm/arch-mpi-controlled-reproduction --suite smoke
python -B validation/mpi/controlled-20261102/run.py \
  --evidence /server/evidence --work /dev/shm/arch-mpi-controlled-reproduction --suite cpu
~~~

Use a fresh evidence directory and staging path for each campaign. Run `profile` separately after the main suite. Two-host runs use `--suite cross --ssh-config /server/control/ssh.json`, where the JSON is an argv list for an already-authorized SSH command with strict host-key verification. Both hosts need matching binaries, tools, inputs and staging paths. Remote access is a deployment prerequisite, not an MPI ownership layer.

## Interpretation

- Primary time is the slowest rank's driver time minus its output time. Also report launcher wall time and output time.
- Strong scaling holds the total grid and step count fixed. Weak scaling holds cells and steps per rank fixed; different global grids have different CFL timesteps, so this is not an equal-physical-duration comparison.
- One warmup precedes five measured repeats. Keep every sample, including slow runs.
- PMPI durations include waiting. Trace timestamps may be aligned within one host, never between the two hosts.
- Local-versus-cross-host comparisons include differing VM environments. They do not isolate physical network bandwidth.
- Peak RSS summed over ranks is not a simultaneous memory peak.
- Full outputs are losslessly archived and every member hash is checked before staging is removed. Archive or transfer time is outside application timing.
- Further code changes require a demonstrated bottleneck and matched improvement. Keep the existing CFL batching if the evidence does not justify another transport change.

See [the existing MPI implementation](../../../docs/development/MPI-FixedGrid.zh-CN.md) for supported inputs and output layout.

## Source versions and failure handling

The CPU timing campaign retains its frozen integrated build. Integration testing exposed a single-rank CLI error path that called MPI_Abort and lost buffered JSON. The correction finalizes normally for a single rank while retaining multi-rank abort behavior. It changes no successful solver path. Full Debug/Release regression and cross-host measurements use the corrected executable; both executable hashes are recorded separately.

The bounded transport probe exchanges 96 B, 32 KiB and 256 KiB between two ranks, checks received bytes, and reports five samples after warmup. Synchronization and result reduction are outside the timed loop. Run it separately from application timing; it cannot establish physical network bandwidth or explain rank arrival skew by itself.

The halo protocol probe uses the measured 24 KiB (2D) and 192 KiB (3D) face messages. It compares two sequential Sendrecv calls with two posted receives, two posted sends and an immediate Waitall, retaining the same direction tags and four buffers. Both received faces are checked after every sample. A favorable microbenchmark is a reason to test a production candidate, not an application speedup claim.

## Reproduce the before/after application comparison

Supply `--before-binary /server/before/build/bin/ARCH --before-revision <before-sha>`
alongside the preparation arguments above. The candidate is saved as both `ARCH`
and `ARCH-concurrent`; the frozen comparison executable is saved as `ARCH-before`.
Use `--suite paired-cpu` on the four-core host and `--suite paired-cross` with
`--ssh-config` on the two-host launcher. Keep the same evidence and staging paths
on both hosts and provision matching toolchains, binaries, helpers and inputs
before launch. These paired suites use 512 steps in 2D and 128 steps in 3D, with
one warmup and five alternating before/after pairs at two and four ranks.
The extra PMPI profiles are excluded from timing medians.

After copying only the CPU summary to a report directory on the launcher:

~~~sh
python -B validation/mpi/controlled-20261102/summarize_pairs.py \
  --cpu /server/cpu-reports --cross /server/cross-evidence \
  --output /server/paired-analysis.json
~~~

The summary also verifies equal logical halo payloads and exchange counts.
The `run_regression.py` and `run_halo_regression.py` files record the exact
server-local integration and candidate regression invocations; their explicit
build paths must be adapted to another server. They preserve full raw results
and verify each archive member before removing only their own staging directory.

## Accepted result

See [the November 2 validation report](../../../docs/development/MPI-Controlled-20261102.zh-CN.md) for all eight application comparisons, the small two-rank 2D local slowdown, regression counts and exact source/binary hashes. The accepted change uses both existing halo directions concurrently and waits before scattering or reusing the existing workspace.
