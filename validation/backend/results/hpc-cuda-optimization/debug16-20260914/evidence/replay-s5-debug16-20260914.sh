#!/usr/bin/env bash
# Executed inside its own transient user scope with a verified 16 GiB limit.
# Compile only: no GPU workload and no formal timing claim. Original P0 matrix:
# all built-in CUDA routes, KLU/cuDSS/custom networks disabled, serial Debug.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-s5-indicator-20260914
out="$root/build/debug16-20260914-v2"
build="$out/debug"
highfive=/home/ubuntu/projects/ARCH-cuda-v2-mainline-build/task-buildopt-20260901T/build-sm90/_deps/highfive-src
cd "$root"
test ! -e "$out"
test "$(df -Pk "$root" | awk 'NR==2 {print $4}')" -ge 8388608
cg=$(awk -F: '$1=="0" {print $3}' /proc/self/cgroup)
[[ "$cg" == /* && "$cg" == *arch-debug16-s5-20260914-v2.scope ]]
control="/sys/fs/cgroup$cg"
test "$(cat "$control/memory.max")" = 17179869184
test "$(cat "$control/memory.swap.max")" = 0
mkdir -p "$out"
printf '%s\n' "$cg" > "$out/cgroup-path.txt"
for file in memory.max memory.swap.max memory.events memory.peak; do
  if test -r "$control/$file"; then cp "$control/$file" "$out/before-$file";
  else printf 'unavailable on this kernel; not a zero peak\n' > "$out/before-$file"; fi
done
finish() {
  rc=$?
  trap - EXIT
  set +e
  for file in memory.max memory.swap.max memory.events memory.peak memory.current memory.swap.current; do
    if test -r "$control/$file"; then cp "$control/$file" "$out/after-$file" || rc=1;
    elif [[ "$file" == memory.peak ]]; then printf 'unavailable on this kernel; not a zero peak\n' > "$out/after-$file";
    else rc=1; fi
  done
  test "$(cat "$control/memory.max")" = 17179869184 || rc=1
  test "$(cat "$control/memory.swap.max")" = 0 || rc=1
  printf 'DEBUG16_SCOPE_EXIT %s\n' "$rc"
  exit "$rc"
}
trap finish EXIT
git rev-parse HEAD > "$out/source-head.txt"
git status --short > "$out/source-status.txt"
git diff --binary -- . ':(exclude)EOS_toolkit/tables/helmholtz/helm_table.dat' > "$out/source-tracked.patch"
find CMakeLists.txt src cmake -type f -print0 | sort -z | xargs -0 sha256sum > "$out/source-files.sha256"
cmake -S . -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-11 -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_COMPILER=/home/ubuntu/projects/.envs/arch/bin/nvcc \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-11 -DCMAKE_CUDA_ARCHITECTURES=90 \
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_KLU=OFF -DARCH_ENABLE_CUDSS=OFF \
  -DARCH_ENABLE_OPENMP=ON -DARCH_FETCH_SUITESPARSE=OFF \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_SOURCE_DIR_HIGHFIVE="$highfive" \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$build/bin" \
  -DARCH_CUDA_HEAVY_COMPILE_JOBS=1 -DCCACHE_PROGRAM=CCACHE_PROGRAM-NOTFOUND \
  '-DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/time;-f;ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C' \
  '-DCMAKE_CUDA_COMPILER_LAUNCHER=/usr/bin/time;-f;ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C' \
  > "$out/configure.log" 2>&1
for target in arch_cuda_backend ARCH; do
  /usr/bin/time -v cmake --build "$build" --target "$target" --parallel 1 --verbose \
    > "$out/$target.log" 2>&1
  printf 'DEBUG16_TARGET_PASS %s\n' "$target"
done
sha256sum -c "$out/source-files.sha256" > "$out/source-identity-after-build.log"
sha256sum "$build/bin/ARCH" "$build/libarch_cuda_backend.a" > "$out/artifacts.sha256"
# A completed build following an OOM/retry is not a clean bounded pass.
awk '$1=="oom" || $1=="oom_kill" || $1=="oom_group_kill" {if ($2 != 0) exit 1}' "$control/memory.events"
test "$(cat "$control/memory.swap.current")" = 0
printf 'DEBUG16_CLEAN_BUILTINS_BUILD_PASS\n'
