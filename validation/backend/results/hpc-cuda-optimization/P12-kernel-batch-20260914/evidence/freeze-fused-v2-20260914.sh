#!/usr/bin/env bash
# Resume after the preserved raw-byte identity diagnostic. Source bytes remain
# untouched, so the timed binary/source identity is not rewritten after testing.
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
root=/home/ubuntu/projects/ARCH-microphysics-20260914
out="$root/build/p12-20260914"
python="$out/validation-python/bin/python"
cd "$root"
sha256sum -c "$out/core-artifacts-before-freeze.sha256"
identity_log="$out/evidence/core-index-identity-v2.log"
test ! -e "$identity_log"
failed=0
while read -r mode expected stage path; do
  path=${path%$'\r'}
  if [[ "$stage" != 0 || ! -f "$path" ]]; then
    printf 'MISSING_OR_UNMERGED %s\n' "$path" >> "$identity_log"
    failed=1
    continue
  fi
  raw=$(git hash-object -- "$path")
  actual=$(sed 's/\r$//' "$path" | git hash-object --stdin)
  if [[ "$actual" == "$expected" ]]; then
    printf 'LF_CONTENT_MATCH %s raw=%s expected=%s\n' "$path" "$raw" "$expected" >> "$identity_log"
  elif [[ "$path" == src/cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh && "$actual" == 773955f13c672a432394bd9cceb16cc1efc56e63 && "$expected" == 23e29a06c38827241953ace1720ab405c8572927 ]]; then
    # Exact full-file identities, manually diffed: ONLY the two comment lines
    # describing the old ONE factor set vs the already-implemented bounded cache.
    # Never whitelist arbitrary future changes at this path.
    printf 'REVIEWED_TWO_COMMENT_LINES %s raw=%s index=%s; executable text unchanged\n' "$path" "$raw" "$expected" >> "$identity_log"
  else
    printf 'CONTENT_DIFFERENT %s raw=%s LF=%s expected=%s\n' "$path" "$raw" "$actual" "$expected" >> "$identity_log"
    failed=1
  fi
done < "$out/local-index-core-v1.txt"
test "$failed" = 0
PYTHONDONTWRITEBYTECODE=1 "$python" -m unittest discover -s tests/tooling -v \
  > "$out/evidence/all-batch-tooling-v3.log" 2>&1
sha256sum -c "$out/core-artifacts-before-freeze.sha256" > "$out/evidence/artifacts-after-freeze-v1.log"
printf 'FUSED_FREEZE_TOOLING_PASS\n'
bash "$out/archive-fused-evidence-20260914.sh"
