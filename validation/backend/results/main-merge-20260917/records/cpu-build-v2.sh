#!/usr/bin/env bash
set -euo pipefail
cd /home/ubuntu/projects/ARCH-main-merge-20260917
test "$(cat logs/cpu-build-v1.exit)" = 1
test "$(df --output=avail -k . | tail -n 1)" -gt 2097152
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export CCACHE_DISABLE=1 OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1
# v1 configured successfully but named an obsolete target; no source changed.
/usr/bin/time -v timeout 1800 cmake --build cpu-build-v1 --parallel 1 --target \
    ARCH arch_predictive_amr_recorder arch_topology_transaction arch_amr_operation_plans \
    arch_checkpoint_compatibility arch_runtime_probe_capabilities
ctest --test-dir cpu-build-v1 --output-on-failure -R '^(predictive_amr_recorder|topology_transaction|amr_operation_plans|checkpoint_compatibility|runtime_probe_and_capabilities)$'
