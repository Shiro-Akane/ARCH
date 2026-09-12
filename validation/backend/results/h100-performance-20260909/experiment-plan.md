# H100 comparison of friend main, 2026-09-09

Baseline source: `0266d96f20b184d4b17ebc6a066ac3b9021f1642` from
`Shiro-Akane/ARCH main`. Production source and numerical budgets are unchanged.

The original documented workload is 2D Sedov, HLLC/PPM/RK3, two AMR levels,
terminal time 0.02, root blocks 4x4 and 8x8, regrid interval 2, 8 OpenMP
threads, one warmup and three measured repeats per backend. Use the committed
`maintenance-freeze-20260908/run_sedov_amr_timing.py` unchanged, including its
field, conservation, accepted-step and topology-alignment checks. Alternate
backend order and exclude validator time from ARCH end-to-end time.

Server: H100-20C vGPU (SM90, 20 GiB), Xeon Gold 6338 guest with 32 vCPUs.
Use the same Release executable for CPU and CUDA, GCC 11.4/CUDA 12.8. Both
lanes use identical eight-thread OpenMP binding. KLU/cuDSS are disabled because
this Sedov workload uses no burning; all built-in EOS/network owners remain
in the production build. Record the compiler difference from the friend's
GCC 12/CUDA 12.3 setup. Do not interpret this vGPU as a full bare-metal H100.

After the matched baseline, run separate diagnostic measurements with the
same input and an LD_PRELOAD CUDA-runtime observer. It adds no CUDA events or
synchronization and preserves every API argument/return value. Reported API
times are host elapsed call latencies, not GPU kernel durations; keep these
runs out of uninstrumented medians and validate their checkpoints. Use
existing backend traces for kernel/copy/synchronization counts and existing
regrid logs for the nested regrid fraction. Any further scale/thread probes
must be labeled separately from the exact original reproduction.

This task diagnoses performance and proposes evidence-backed optimization.
No production optimization is part of this experiment commit.
