# Native Sod checkpoint continuation

Using saved UatSodRestart.par, ARCH Studio's native Restart confirmation launched
one independent terminal. Core loaded the actual source checkpoint at t=0.05,
step 67 and succeeded at t=0.2, step 280. It did not run the initial .par again.

The existing compareRestartCheckpoints.mjs checked all 24 object paths, root/object
attributes, 21 dataset layouts and all values against the retained uninterrupted
source final checkpoint: exact equality, maxAbs 0, nonfinite values rejected.
The selected checkpoint SHA and saved input/binary SHA remain unchanged.
Core/worker exited; the completed terminal remains held by design.

This is fixed-grid CPU continuation engineering evidence. It does not establish
independent scientific accuracy, adaptive AMR/CUDA or complete desktop acceptance.
Window, confirmation and terminal captures accompany the continuation record.
Visibility, cancellation, close and Stop are outside its verified scope;
[platform coverage](../../../../docs/releases/PlatformSupport.md) lists desktop checks.

summary.json contains identities, reduced metrics and local raw inventory only.
Raw H5/checkpoints/full console logs stay in ignored persistent local directories.
No raw arrays are included. Current source HEAD `cf572e0f` was clean when launched;
binary SHA `e506619f` has the managed Build record from clean `f5e2c001`.
Full dependency freshness remains unknown.
