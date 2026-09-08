# Preserved dynamic-3D attempt 913

`replay.py` is a byte-identical archive of the original six-snapshot recipe,
SHA-256 `c0ea08f23cb869d00b3e67073b8f22630babd0db1fbb624f0cc10dd8500e1b4a`.
Its original relative-path expressions are preserved; this copy is evidence,
not an entry point to run from the deeper archive directory.

Attempt 913 ended with a [coverage failure](../release-913/failure.json):
the sampled leaf sets did not expose an explicit coarsening transition.
The [execution log](../../../../../build/dynamic-3d-final-913.log) remains
unchanged. CPU and CUDA regrid logs show intermediate refinement and coarsening
between the step-40 and step-80 snapshots. The successor adds only step 41 to
the observation list; physical inputs, thresholds, capacity and numerical
budgets stay unchanged. It must pass independently before closing this gate.
