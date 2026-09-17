#!/usr/bin/env bash
set -euo pipefail
cd /home/ubuntu/projects/ARCH-main-merge-20260917
test "$(cat logs/recorder-build.exit)" = 0
test "$(cat logs/recorder-run.exit)" = 0
test ! -e cpu-build-v1
test "$(df --output=avail -k . | tail -n 1)" -gt 2097152
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export CCACHE_DISABLE=1
export OMP_NUM_THREADS=1
export OPENBLAS_NUM_THREADS=1
cmake -S source -B cpu-build-v1 -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS_DEBUG=-O1 -g0' \
    -DCMAKE_C_COMPILER=/usr/bin/gcc-11 -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
    -DARCH_ENABLE_CUDA=OFF -DARCH_ENABLE_KLU=OFF -DARCH_FETCH_SUITESPARSE=OFF \
    -DBUILD_TESTING=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_HIGHFIVE=/home/ubuntu/projects/ARCH-cuda-v2-mainline-build/task-buildopt-20260901T/build-sm90/_deps/highfive-src \
    -DARCH_RUNTIME_OUTPUT_DIRECTORY=/home/ubuntu/projects/ARCH-main-merge-20260917/cpu-build-v1/bin
/usr/bin/time -v timeout 1800 cmake --build cpu-build-v1 --parallel 1 --target \
    ARCH arch_predictive_amr_recorder arch_topology_transaction arch_amr_operation_plans \
    arch_checkpoint_compatibility arch_checkpoint_restart arch_runtime_probe_capabilities
ctest --test-dir cpu-build-v1 --output-on-failure -R '^(predictive_amr_recorder|topology_transaction|amr_operation_plans|checkpoint_compatibility|checkpoint_restart|runtime_probe_and_capabilities)$'
