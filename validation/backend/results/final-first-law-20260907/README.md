# Focused backend instrumentation

Chinese translation: [README.zh-CN.md](README.zh-CN.md). English is authoritative.

The [memcheck record](memcheck-929/evidence.json) and
[racecheck record](racecheck-903/evidence.json) each pass all 23 focused routes.
Memcheck reports zero errors and zero leaked bytes or allocations; racecheck
reports zero hazards, errors or warnings. Each route has its own complete report.
The independent read-only reviews for [memcheck](memcheck-929-review.json) and
[racecheck](racecheck-903-review.json) confirm the exact executable inventory,
argument lists, file identities and sparse trajectory coverage using the existing
validation helpers.

| Campaign | Status | Sparse observation interval | Coverage |
| --- | --- | --- | --- |
| [Memcheck](memcheck-929/evidence.json) | Complete | `1e-10 s`, original default | 23 routes; 23 complete, clean reports |
| [Racecheck](racecheck-903/evidence.json) | Complete | Explicit `1e-12 s` profile | 23 routes; 23 complete, clean reports |

The profiles separate instrumentation duration from scientific endpoints. The
racecheck observation does not replace the full-interval
[audit31 trajectory](../../../network/results/sparse-native-20260907/release-900/evidence.json)
or [ARCH application](../../../network/results/runtime-native-20260907/release-875/backend-validation-evidence.json)
checks. Overall release acceptance also includes the application-level safety
records and final delivery review in the [validation guide](../../../README.md).

## Completed instrumentation coverage

The inventory combines 19 configured tests with four explicit generated-network
routes. It covers AMR transactions, migration, exchange, composition and
geometry; sparse solvers; EOS failure handling and host/device parity; built-in
NSE and thermal burn mathematics; generated-network mathematics; all three
aprox19 ODE routes; controlled weak trajectories and owner reuse; and real
audit31 sparse evolution. The resolved commands match the
[98-test Release inventory](release-regression-895/evidence.json).

Both campaigns' full source, build and execution-control records agree with that
Release record and the ordinary audit31 trajectory record. All 23 executable
identities and each campaign's 23 distinct sanitizer reports were checked. Only
the racecheck sparse observation interval differs from the original argument
lists. The frozen recipe SHA-256 is
`a4c505dcf3e1507d5bb7888a90f1e7663d78040f11719993f228dc9fa2e32be8`.

### Full-interval sparse trajectory under memcheck

The audit31 input remains `rho=1e7`, `T=3e9`, `cv=1e8`, local tolerance `1e-7`,
and equal initial C12/O16 mass fractions. Its total interval is `1e-10 s`, split
into four subdivisions. BE_NR, BD and ROS4 each use both two- and three-cell
storage, with CPU KLU and CUDA cuDSS. The shared transcript parser verifies
24 CPU step records, 24 GPU step records, six final states and three method
summaries; no ODE or storage case is omitted.

| ODE | Maximum field error | Maximum limiter error | Recorded CUDA kernel launches | Workspace bytes per lane |
| --- | ---: | ---: | ---: | ---: |
| BE_NR | `5.3164e-15` | `5.4667e-15` | 137,946 | 7,204 |
| BD | `1.8812e-14` | `1.8305e-14` | 6,164 | 23,740 |
| ROS4 | `5.1266e-15` | `5.0365e-15` | 8,932 | 11,852 |

The original field and limiter budgets remain `2e-10` and `2e-8`. Every method
reports measurable evolution; the smallest method evolution metric is
`2.56994e-5`. Pool capacity stays at two across both storage sizes. These are
instrumented solver/provider checks, with the same shared mathematics as the
ordinary run. Kernel counts and instrumented timings are diagnostics, not
controlled performance comparisons.

### Sparse observation under racecheck

Racecheck uses the explicitly selected `1e-12 s` observation interval. Density,
temperature, heat capacity, local tolerance, composition, four subdivisions,
all three ODEs and both storage sizes remain the same. Its transcript also
contains all 24 CPU and 24 GPU steps, six final states and three method summaries.

| ODE | Maximum field error | Maximum limiter error | Recorded CUDA kernel launches | Workspace bytes per lane |
| --- | ---: | ---: | ---: | ---: |
| BE_NR | `8.2953e-15` | `7.0884e-15` | 3,820 | 7,204 |
| BD | `1.1074e-14` | `1.1026e-14` | 2,252 | 23,740 |
| ROS4 | `1.5222e-14` | `1.5148e-14` | 492 | 11,852 |

The same `2e-10` field and `2e-8` limiter budgets pass. Every method has positive
kernel activity and measurable evolution, with a minimum method evolution
metric of `2.54829e-7`; pool capacity remains two. This record checks device races
during real sparse evolution. Scientific and whole-application acceptance retain
their original `1e-10 s` endpoints.

## Reproduce the campaigns

Run from the repository root with a testing-enabled CUDA build containing the
registered audit31 and weak_urca packages, CPU KLU, CUDA cuDSS and required EOS
data. Follow the [network setup](../../../network/README.md#reproduce-the-records)
to generate the packages and configure the build. These records and their
acceptance index use Python 3.11, including identical rounding of diagnostic
timing sums.

Build the complete configured test inventory before running the 23-route recipe;
the network guide's five explicit targets cover its focused science and
application commands. For the build directory used below, a guarded default
build includes the remaining tests:

```bash
python3 -B tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-cuda --parallel 1
```

Use new output directories and dispatch GPU campaigns serially under the
existing resource guard. Point `--sanitizer` and `--nvidia-smi` to their installed
executables. The explicit timeouts are per-route allowances, not runtime estimates:

```bash
OMP_NUM_THREADS=4 python3 -B tools/run_memory_guarded.py \
  --min-available-mib 1536 --max-swap-growth-mib 256 --pressure-guard \
  --gpu-memory-device 0 --nvidia-smi /path/to/nvidia-smi \
  --log build/focused-memcheck-new.log -- \
  python3.11 -B validation/backend/results/final-first-law-20260907/run_sanitizers.py \
  --build-dir build-cuda \
  --output-dir validation/backend/results/focused-memcheck-new \
  --sanitizer /path/to/compute-sanitizer --tool memcheck --timeout-seconds 2400

OMP_NUM_THREADS=4 python3 -B tools/run_memory_guarded.py \
  --min-available-mib 1536 --max-swap-growth-mib 256 --pressure-guard \
  --gpu-memory-device 0 --nvidia-smi /path/to/nvidia-smi \
  --log build/focused-racecheck-new.log -- \
  python3.11 -B validation/backend/results/final-first-law-20260907/run_sanitizers.py \
  --build-dir build-cuda \
  --output-dir validation/backend/results/focused-racecheck-new \
  --sanitizer /path/to/compute-sanitizer --tool racecheck --timeout-seconds 86400 \
  --sparse-race-interval 1e-12
```

The second command reproduces the completed racecheck observation profile.
Without `--sparse-race-interval`, both tools retain
the full `1e-10` interval. The option is accepted only for racecheck and must be
positive and finite. All other 22 commands, sparse inputs, four subdivisions,
three ODEs, both storage sizes and numerical budgets stay unchanged.

The [recipe tests](test_run_sanitizers.py) are CPU-only and can be run with
`python3.11 -B validation/backend/results/final-first-law-20260907/test_run_sanitizers.py`.

## Retained earlier attempts

[Attempt 902](attempt-902/README.md) reached 22 routes before the sparse route
exceeded its original timeout. [Attempt 916](attempt-916/README.md) was stopped
by the resource monitor before completing its inventory. Their original
failed records remain intact; 929 is the complete memcheck successor. The
[pre-profile recipe archive](profile-history/full-interval/README.md) records
the unchanged defaults and the explicit racecheck observation option.
