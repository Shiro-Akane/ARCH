# Native Sod checkpoint continuation

Using saved UatSodRestart.par, ARCH Studio's native Restart confirmation launched
one independent terminal. Core loaded the actual source checkpoint at t=0.05,
step67 and succeeded at t=0.2, step280. It did not run the initial .par again.

The existing compareRestartCheckpoints.mjs checked all24 object paths, root/object
attributes,21 dataset layouts and all values against the retained uninterrupted
source final checkpoint: exact equality, maxAbs0, nonfinite values rejected.
The selected checkpoint SHA and saved input/binary SHA remain unchanged.
Core/worker exited; the completed terminal remains held by design.

This is fixed-grid CPU continuation engineering evidence. It does not establish
independent scientific accuracy, adaptive AMR/CUDA or complete desktop acceptance.
Native window/confirmation/terminal were captured; the user's latest visibility
answer remains pending and cannot be inferred from capture. Remaining native
cancellation/close/Stop tests are not passed by this record.

summary.json contains identities, reduced metrics and local raw inventory only.
Raw H5/checkpoints/full console logs stay in ignored persistent local directories.
No raw arrays are included. Current sourceHEADcf572e0f was clean when launched;
binary SHAe506619f has the managed Build record from cleanf5e2c001.
Full dependency freshness remains unknown.
