# Execution-cost follow-up

This record identifies the execution changes tested after the user-boundary
acceptance baseline `d45ea2956` on `codex/o8-boundaries`. It preserves the
existing shared mathematics, strict FP64, CFL/AMR inputs, complete face EOS and
both Strang burning half-steps. The [Chinese assessment](Report.zh-CN.md)
explains retained changes, withdrawals, scientific coverage and remaining
performance targets.

The [processed evidence](summary.json) contains all three alternating
CPU/FLASH samples for each of seven matched physical endpoints, source/input
and executable identities, stage costs and original-budget field comparisons.
Six cases are below twice FLASH's complete-process time; five are below 1.5
times. The fine Cellular case remains at 2.867 times, and Sod at 1.746 times.
These are same-task measurements, rather than demonstrated equal-error costs
or proof of an irreducible performance gap. The five-step 2D/3D four-module
checks have a separate capability scope.

[Delegation evidence](delegation.json) reports genuine DeepSeek jobs, repairs,
independent review decisions, estimated token costs and known measurement
limits. Original HDF5/plotfiles, checkpoints, executables and worker logs stay
on the producing machine.

## Reproduction

Use the existing [comparison entry point](../../run_comparison.py), with numpy
and h5py available, after a Release CPU build. A local FLASH installation is
needed only for this optional developer comparison. Pass the existing reference
Helm binary table rather than adding a first-run conversion penalty:

```bash
python3 validation/gravity/flash/run_comparison.py \
  --arch-cpu bin/ARCH --routes arch flash \
  --cases sod jeans64 jeans128 a b noburn fine \
  --threads 8 --ranks 8 --affinity 0,2,4,6,8,10,12,14 --repeats 3 \
  --flash-root /path/to/FLASH4.8 \
  --helm-binary /path/to/helm_table.bdat \
  --prefix cpu-followup-new --output output/cpu-followup-new \
  --manifest output/cpu-followup-new.json
```

The recorded affinity is specific to this i7-10700 host; select physical cores
appropriate to the destination machine. Preserve the fixtures and verify actual
checkpoint times before interpreting a cost ratio. The original scientific
checks remain in the [test guide](../../../../../tests/README.md) and
[gravity qualification entry](../../../check_cuda_compatibility.py).
ARCH's build, tests and CI remain independent of FLASH. Compile, scientific
qualification and instrumentation finish before formal performance measurement;
heavy jobs run serially.

The frozen source passes CPU 61/61, CUDA 125/125, the existing gravity
qualification entry and the current 2D/3D coupled field comparisons. On the
RTX 3060 Ti, the 262,144-cell periodic case reaches physical `t=0.02 s` in
15 accepted steps. Three alternating runs with both lanes bound to the same
physical cores give 1.338 times speedup with one CUDA-enabled executable.
Using the faster measured pure-CPU configuration gives a conservative 1.255
times platform reference; its compiler and sample window differ. The 3D
five-step coupled diagnostic still has negative whole-task speedup.

Repeat this optional local measurement with the existing entry point after
compilation and qualification finish:

```bash
taskset -c 0,2,4,6,8,10,12,14 \
  python3 validation/gravity/check_cuda_compatibility.py \
  --cpu-arch bin/ARCH --cuda-arch build-cuda/bin/ARCH \
  --benchmark-only --benchmark-case large-periodic \
  --benchmark-time 0.02 --benchmark-repeats 3 \
  --output output/gpu-periodic-new
```

Select the destination host's physical-core affinity, record the CPU thread
scan and preserve slower/negative results. The initial unbound batch and the
same-compiler CPU baseline comparison remain separate in the processed record;
neither is mixed into the final paired backend median.
