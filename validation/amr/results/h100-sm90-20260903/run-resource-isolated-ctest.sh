#!/usr/bin/env bash
set -u

build_dir=${1:?usage: run-resource-isolated-ctest.sh BUILD_DIR OUTPUT_DIR}
output_dir=${2:?usage: run-resource-isolated-ctest.sh BUILD_DIR OUTPUT_DIR}
ctest_bin=${CTEST_BIN:-/home/ubuntu/projects/.envs/arch/bin/ctest}

mkdir -p "${output_dir}"
while nvidia-smi --query-compute-apps=pid --format=csv,noheader,nounits \
    | grep -q '[0-9]'; do
    sleep 30
done

date -Is > "${output_dir}/resource-isolated-ctest.log"
nvidia-smi --query-gpu=memory.total,memory.used,memory.free \
    --format=csv,noheader \
    >> "${output_dir}/resource-isolated-ctest.log"

"${ctest_bin}" --test-dir "${build_dir}" \
    -R '^(network_nse_device|cuda_backend_burn_aprox(19|21)_(be_nr|bd|ros4)|burn_policy_parity_aprox(19|21)_(be_nr|bd|ros4))$' \
    --output-on-failure -j1 \
    >> "${output_dir}/resource-isolated-ctest.log" 2>&1
focused_status=$?
printf 'FOCUSED_EXIT=%s\n' "${focused_status}" \
    >> "${output_dir}/resource-isolated-ctest.log"

if [[ ${focused_status} -eq 0 ]]; then
    "${ctest_bin}" --test-dir "${build_dir}" --output-on-failure -j1 \
        > "${output_dir}/full-ctest-isolated.log" 2>&1
    full_status=$?
else
    full_status=125
fi
printf 'FULL_EXIT=%s\n' "${full_status}" \
    >> "${output_dir}/resource-isolated-ctest.log"
printf '%s %s\n' "${focused_status}" "${full_status}" \
    > "${output_dir}/resource-isolated-ctest.done"

if [[ ${focused_status} -ne 0 ]]; then
    exit "${focused_status}"
fi
exit "${full_status}"
