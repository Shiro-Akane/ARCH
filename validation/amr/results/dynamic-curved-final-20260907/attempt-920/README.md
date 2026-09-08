# Attempt 920: retained failure and bounded lifecycle successor

Attempt 920 remains **failed**. Its original [attempt record](../release-920/attempt.json),
[failure record](../release-920/failure.json), logs and checkpoints have not been
rewritten. The [archived recipe](replay.py) is a byte-for-byte copy of the recipe
that produced them. It is retained for review, not directly executable at this
deeper location: the original script resolves the repository from its original
directory.

The successor is **release-923, not yet run**. It observes both original curved
cases at steps `2, 5, 20, 80, 160`. Only the observation horizon changed; the
original input, overrides, two geometries, capacity, refinement thresholds,
1200-second per-lane timeout, reduction/conservation budgets, RKL1 four-stage
policy and explicit bidirectional topology checks remain unchanged. This is an
observation window for the refine/coarsen lifecycle, not a claim to validate a
fixed 320-step trajectory. The spherical route must still run and pass.

## What failed

Both cylindrical executors completed all six original observation runs,
including step 320. The CUDA step-320 lane was rejected by
`tools/validate_backend_results.py::validate_cuda_diffusion_schedule` before
`run_case` could return its checkpoint/conservation records. The exact exception
was `CUDA diffusion schedule order/stage count drifted`. No spherical lane had
started. The guard did not stop the run.

The 320-step CUDA schedule contains 640 RKL1 records: 634 use four stages and six
use three stages, covering the two diffusion lanes at macro steps 265–267. The
last coarse transition, 22 to 16 leaves at macro step 265, increases the
forward-Euler limit from approximately `5.39248e-5` to `1.23020e-4`. The
accepted-step controller then grows the time step by its existing factor 1.2;
four stages resume at macro step 268.

This follows the existing shared stage-selection function in
[src/numerics/diffusion/DiffFunction.cpp](../../../../../src/numerics/diffusion/DiffFunction.cpp)
and step-growth control in
[src/driver/DriverControl.h](../../../../../src/driver/DriverControl.h).
The fixed-stage validator does not support an adaptive stage policy. No parser,
formula, production code or tolerance was changed to reinterpret that failure.

## Separate read-only diagnosis

[diagnosis.json](diagnosis.json) records a subsequent CPU-only review of the
already-written checkpoints, using the existing comparator and validation
helpers. It includes the full observed source/build/environment identity,
original policies, checkpoint/parameter hashes, checked schedule identities and
regrid summaries. All 72 retained diagnostic-file identities were rechecked.
This is postmortem evidence, **not a successful original runner report** and not
an independent exact solution to the Gaussian problem.

- All six original CPU/CUDA checkpoint comparisons pass the original
  `rtol=1e-8, atol=5e-12` field budgets.
- All twelve initial-to-final mass/energy/rhoX comparisons pass the original
  physical-cell-volume `rtol=2e-12, atol=2e-11` conservation budgets.
- At step 320, time is `0.2143845026424202`, both backends have 16 leaves, and
  the maximum absolute field difference is `7.771561172376096e-16`.
- Across all checkpoints, mass drift is zero; maximum absolute energy drift is
  `1.865174681370263e-14`, and maximum absolute rhoX drift is
  `2.1337098754514727e-16`.
- The unchanged four-stage schedule check passes at steps 2, 5, 20, 80 and 160.
  Its step-320 failure is recorded explicitly in the diagnosis.

The lifecycle objective is already observed within the 160-step window.
Excluding initialization, the cylindrical run has four refinement and five
coarsening transactions by step 160, reaches at most 64 leaves, and finishes
with 34 mixed-level leaves. The shared checker verifies complete parent/four-child
refinement and coarsening relationships between actual saved snapshots.
The previously inspected parent keys `(0,4,0,0)` and `(0,4,1,0)` exist at step 2,
are replaced by their complete child sets at step 5, and return at step 160.
CPU and CUDA runtime transition histories agree. Full return to a uniform mesh
by step 320 is not required to demonstrate this bidirectional lifecycle.

The shorter successor does not discard a scientific mismatch: the retained
320-step field and conservation checks also pass. It preserves the original
fixed-four-stage witness in a window that already spans the requested
lifecycle, without adding an adaptive validation mechanism during source
freeze. Success on the as-yet-unrun spherical route is not inferred from the
cylindrical result.

## Identities

| Artifact | SHA-256 |
| --- | --- |
| Archived original recipe | `ef719fae9370b49cbfaa3c5c1eb49f8c5aa3f85aac21f7b2c6dc5b164a91c640` |
| Original attempt.json | `f62f110270fa419eaf916c915c6bdb3d1583d46c0dc46fd0c77222802e5f3909` |
| Original failure.json | `9f1866644f20637833255d5595b530b956b81f8667c81450049482a525f9f841` |
| Original guard log | `7a9198b50c1f9f0918123ee57115a8ab525f83333fefde8f4f8c5fa9e4595b35` |
| Read-only diagnosis.json | `d8a6f8448a38668f9c5a9f81da9d14676361d34d054c2f18ad2a210afa428fcd` |
| Successor recipe | `da183845ae7107de94f1e70eec0e07ac8615ae0d0abc5d4ecdf5879e17312d8e` |

Frozen source worktree SHA-256 is
`73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171`;
this is not a Git commit ID. ARCH and the comparator remain respectively
`9bba8f657a46ccfdf5b264387b9269566b6a7e724759027f63e4cd45aa7fe3ba`
and `e5cc53d3d19a8ad547f1d990353cc55d55d884ce24dcd2e4f99621dd50825bbc`.
Original OMP4 execution and subsequent OMP1 read-only review are kept distinct.

## Commands

Run from the repository root. The original uninstrumented recipe command,
recorded inside the guarded attempt, was:

```bash
OMP_NUM_THREADS=4 /usr/bin/python3 -B \
  validation/amr/results/dynamic-curved-final-20260907/replay.py \
  --build-dir build/release-core-throughput-cmake \
  --output-dir validation/amr/results/dynamic-curved-final-20260907/release-920
```

That command is historical: the live script now has the shorter horizon.
Do not rerun into `release-920` or replace its failure. Its guard transcript is
`build/dynamic-curved-final-920.log`.

The successor command, to be dispatched separately under the existing resource
guard and serial GPU scheduling, is:

```bash
OMP_NUM_THREADS=4 /usr/bin/python3 -B \
  validation/amr/results/dynamic-curved-final-20260907/replay.py \
  --build-dir build/release-core-throughput-cmake \
  --output-dir validation/amr/results/dynamic-curved-final-20260907/release-923
```

The existing optional `--sanitizer PATH --tool memcheck|racecheck` interface is
unchanged. No successor or instrumented execution was performed while preparing
this archive.

The exact read-only diagnostic command below reuses existing helpers and prints
JSON. It runs only comparator `--compare`/`--metrics` operations, not ARCH
simulations or GPU tests. The archived JSON is the observed output; replaying
this command would record a new review time/environment and must not overwrite
that record. Its returned diagnostic success deliberately includes the retained
fixed-stage failure.

```bash
OMP_NUM_THREADS=1 /usr/bin/python3 -B - <<'PY'
from collections import Counter
from datetime import datetime, timezone
import csv
import json
from pathlib import Path
import sys

root = Path.cwd().resolve()
sys.path.insert(0, str(root / 'tools'))
import validate_backend_results as runtime
import validation_provenance as provenance

base = root / 'validation/amr/results/dynamic-curved-final-20260907'
failure_path = base / 'release-920/failure.json'
failure = json.loads(failure_path.read_text())
case = failure['derived_cases'][0]
build = root / 'build/release-core-throughput-cmake'
arguments = dict(arch=build / 'bin/ARCH',
    checkpoint_validator=build / 'arch_cuda_single_level_validation',
    source_root=root, build_dir=build)
before = provenance.capture(**arguments)
for section in ('source', 'build', 'artifacts'):
    assert before[section] == failure['provenance'][section], section
assert failure['status'] == 'failed' and failure['focused_gate_pass'] is False
for item in failure['retained_logs']:
    assert provenance.file_identity(Path(item['path'])) == item
validator = arguments['checkpoint_validator']
records, observations, regrids, schedules = [], [], {}, {}
for steps in case['accepted_steps']:
    paths = {}
    for backend in ('cpu', 'cuda'):
        lane = base / 'release-920/runs' / case['id'] / f'step-{steps}' / backend
        prefix = lane / f"{case['id']}_{backend}_s{steps}"
        paths[backend] = dict(initial=Path(str(prefix) + '_chk_0000.h5'),
            final=Path(str(prefix) + '_chk_0001.h5'),
            parameter=Path(str(prefix) + '.par'))
        regrids.setdefault(str(steps), {})[backend] = runtime.read_regrid_metrics(
            Path(str(prefix) + '_regrid.tsv'), backend, steps)
        if backend == 'cuda':
            schedule = Path(str(prefix) + '_diffusion_schedule.tsv')
            try:
                result = runtime.validate_cuda_diffusion_schedule(schedule, steps, case['rkl_policy'])
                schedules[str(steps)] = dict(status='pass',
                    records=result['records'], order=result['order'], stages=result['stages'],
                    file=provenance.file_identity(schedule))
            except RuntimeError as error:
                assert steps == 320 and str(error) == 'CUDA diffusion schedule order/stage count drifted'
                rows = list(csv.DictReader(schedule.read_text().splitlines(), delimiter='\t'))
                schedules[str(steps)] = dict(status='failed', error=str(error),
                    file=provenance.file_identity(schedule), records=len(rows),
                    stage_counts=dict(Counter(row['stages'] for row in rows)),
                    order_counts=dict(Counter(row['order'] for row in rows)),
                    non_four_stage_rows=[row for row in rows if row['stages'] != '4'])
    parity = runtime.compare_hdf5_checkpoints(paths['cpu']['final'], paths['cuda']['final'],
        case['reduction_policy'], validator,
        comparison_mode=runtime.checkpoint_comparison_mode(case))
    assert parity['passed'] is True
    observations.append(parity)
    conservation = {backend: runtime.validate_conservation(
        validator, paths[backend]['initial'], paths[backend]['final'],
        case['conservation_policy'], parameter_file=paths[backend]['parameter'],
        parameter_sha256=provenance.sha256(paths[backend]['parameter']))
        for backend in ('cpu', 'cuda')}
    records.append(dict(steps=steps, parity={key: value for key, value in parity.items()
        if key != 'topology'}, conservation=conservation,
        files={backend: {name: provenance.file_identity(path) for name, path in lane.items()}
            for backend, lane in paths.items()}))
through160 = runtime.validate_topology_policy(observations[:-1],
    case['topology_policy'], case['id'])
all_snapshots = runtime.validate_topology_policy(observations,
    case['topology_policy'], case['id'])
summary = {}
for steps in ('160', '320'):
    summary[steps] = {}
    histories = []
    for backend in ('cpu', 'cuda'):
        metric = regrids[steps][backend]
        initial, *running = metric['records']
        changes = [row for row in running if row['topology_changed']]
        histories.append([(row['macro_step'], row['physical_time'], row['old_blocks'],
            row['new_blocks']) for row in changes])
        summary[steps][backend] = dict(file=metric['file'], summary=metric['summary'],
            initialization=initial, runtime_refine_transactions=sum(
                row['new_blocks'] > row['old_blocks'] for row in changes),
            runtime_coarsen_transactions=sum(
                row['new_blocks'] < row['old_blocks'] for row in changes),
            runtime_changes=changes)
    assert histories[0] == histories[1]
after = provenance.capture(**arguments)
provenance.require_unchanged(before, after)
result = dict(schema=1, scope='Read-only postmortem of retained attempt 920 checkpoints',
    status='diagnostic-checks-pass', release_qualified=False,
    original_campaign_status='failed', original_focused_gate_pass=False,
    spherical_route_status='not-run', recorded_utc=datetime.now(timezone.utc).isoformat(),
    check_mode='Rechecked with existing CPU-only comparator and validation helpers; no simulation replay',
    failure=provenance.file_identity(failure_path),
    attempt=provenance.file_identity(base / 'release-920/attempt.json'),
    archived_recipe=provenance.file_identity(base / 'attempt-920/replay.py'),
    guard_log=provenance.file_identity(root / 'build/dynamic-curved-final-920.log'),
    retained_log_identities_rechecked=len(failure['retained_logs']),
    original_case=case, provenance=before, identity_verified_after_diagnostic=True,
    checkpoints=records, fixed_four_stage_checks=schedules,
    topology_through_160=through160, topology_all_six_snapshots=all_snapshots,
    regrid_observations=summary,
    note='Diagnostic parity/conservation success does not repair the original fixed-stage failure or qualify the campaign. The successor has not been run.')
print(json.dumps(result, indent=2, default=str))
PY
```

The successor's pure self-tests are:

```bash
OMP_NUM_THREADS=1 /usr/bin/python3 -B \
  validation/amr/results/dynamic-curved-final-20260907/replay.py --self-test
```

They check original-case inheritance, exact equality to the failed attempt's
derived cases after restoring only the removed step 320, and positive/negative
complete parent/four-child topology fixtures.
