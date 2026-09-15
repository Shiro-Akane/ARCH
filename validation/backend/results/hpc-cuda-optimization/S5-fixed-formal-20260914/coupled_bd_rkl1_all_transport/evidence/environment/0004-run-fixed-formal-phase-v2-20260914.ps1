param([Parameter(Mandatory=$true)][string]$Module)
$ErrorActionPreference='Stop'
$allowed=@('diffusion_rkl1','diffusion_rkl2','burn_be_nr','burn_bd','burn_ros4')
foreach ($ode in @('be_nr','bd','ros4')) {
    foreach ($diff in @('rkl1','rkl2')) { $allowed += "coupled_${ode}_${diff}_all_transport" }
}
if ($Module -notin $allowed) { throw 'Unknown phase' }
$repo='C:\tmp\ARCH-perf-20260909'
Set-Location -LiteralPath $repo
$server='ubuntu@100.97.101.5'
$root='/home/ubuntu/projects/ARCH-multiphysics-fix-20260914'
$recipes='/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914'
$base="$root/build/fix-20260914/timing"
$label="formal-$Module-v1"
$stem="fixed-formal-$Module-v1"
$destination=Join-Path $repo "validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914/$Module"
$extract=Join-Path $repo "build/fixed-formal-compact-$Module-v1"
if ((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath $extract)) { throw 'Output already exists' }
# Product backup is separately completed and SHA verified before this driver.
$product=Get-Content -LiteralPath 'build/fixed-timing-products-archive.json' -Raw | ConvertFrom-Json
$productPath=Join-Path $repo 'build/fixed-timing-products-v1.tar.zst'
if ((Get-Item -LiteralPath $productPath).Length -ne $product.bytes -or
    (Get-FileHash -LiteralPath $productPath -Algorithm SHA256).Hash.ToLower() -ne $product.sha256) { throw 'Product backup mismatch' }
$run=@'
set -euo pipefail
export PATH=/home/ubuntu/projects/.envs/arch/bin:$PATH
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-multiphysics-fix-20260914
recipes=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914
python="$recipes/validation-python/bin/python"
base="$root/build/fix-20260914/timing"
module=__MODULE__
label="formal-$module-v1"
cd "$root"
test ! -e "$base/$label"
if pgrep -x ARCH >/dev/null; then echo 'Other ARCH process exists; do not time' >&2; exit 2; fi
if pgrep -x nvcc >/dev/null || pgrep -x ptxas >/dev/null; then echo 'Compilation exists; do not time' >&2; exit 2; fi
test -z "$(nvidia-smi --query-compute-apps=pid --format=csv,noheader)"
mkdir -p "$base"
vmstat 1 3 > "$base/$label-idle-preflight.log"
# Skip the since-boot first sample; require the two new observations to be
# mostly idle and without swap traffic. This is not exclusive-host proof.
awk 'NR>3 { n++; if ($15<90 || $7!=0 || $8!=0) bad=1 } END { exit (n!=2 || bad) }' \
  "$base/$label-idle-preflight.log"
printf 'S5_FORMAL_PHASE_BEGIN %s\n' "$module"
timeout --signal=INT --kill-after=30s 24h "$python" tools/run_memory_guarded.py \
  --min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard --gpu-memory-device 0 \
  --log "$base/$label-child.log" -- bash "$recipes/replay-fixed-timing-20260914.sh" formal "$module" \
  > "$base/$label-guard.log" 2>&1
printf 'S5_FORMAL_PHASE_SAMPLES_PASS %s\n' "$module"
"$python" "$recipes/archive-formal-timing-v2-20260914.py" "$module" > "$base/collection-$module-v1.log" 2>&1
printf 'S5_FORMAL_PHASE_ARCHIVE_PASS %s\n' "$module"
'@
$run=$run.Replace('__MODULE__',$Module)
ssh -o BatchMode=yes -o ConnectTimeout=12 -o ServerAliveInterval=10 -o ServerAliveCountMax=3 $server $run
if ($LASTEXITCODE -ne 0) { throw 'Formal phase/collection failed; originals retained' }
scp -o BatchMode=yes "${server}:$base/$label-archive/compact/raw-archive.json" "build/$stem-raw-archive.json"
if ($LASTEXITCODE -ne 0) { throw 'Archive metadata download failed' }
$raw=Get-Content -LiteralPath "build/$stem-raw-archive.json" -Raw | ConvertFrom-Json
if ($raw.module -ne $Module -or $raw.status -ne 'passed' -or $raw.runs -ne 108 -or $raw.comparisons -ne 105) { throw 'Unexpected archive qualification' }
scp -o BatchMode=yes "${server}:$root/build/$stem.tar.zst" "build/$stem.tar.zst"
if ($LASTEXITCODE -ne 0) { throw 'Raw backup failed' }
$rawPath=Join-Path $repo "build/$stem.tar.zst"
$checksum=(Get-FileHash -LiteralPath $rawPath -Algorithm SHA256).Hash.ToLower()
if ((Get-Item -LiteralPath $rawPath).Length -ne $raw.bytes -or $checksum -ne $raw.sha256) { throw 'Raw backup checksum mismatch' }
scp -o BatchMode=yes "${server}:$root/build/$stem-compact.tar.zst" "build/$stem-compact.tar.zst"
if ($LASTEXITCODE -ne 0) { throw 'Compact download failed' }
$remoteHash=ssh -o BatchMode=yes $server "sha256sum '$root/build/$stem-compact.tar.zst'"
if ($LASTEXITCODE -ne 0) { throw 'Compact remote hash failed' }
$localHash=(Get-FileHash -LiteralPath "build/$stem-compact.tar.zst" -Algorithm SHA256).Hash.ToLower()
if ($localHash -ne ($remoteHash -split '\s+')[0]) { throw 'Compact backup checksum mismatch' }
New-Item -ItemType Directory -Path $extract | Out-Null
tar -xf "build/$stem-compact.tar.zst" -C $extract
if ($LASTEXITCODE -ne 0) { throw 'Compact extraction failed' }
New-Item -ItemType Directory -Path $destination | Out-Null
Copy-Item -LiteralPath "$extract/compact" -Destination "$destination/evidence" -Recurse
$cleanup="export PYTHONDONTWRITEBYTECODE=1; '$recipes/validation-python/bin/python' '$recipes/compact-verified-formal-hdf-20260914.py' '$Module' --local-verified-sha256 '$checksum' --local-verified-bytes '$($raw.bytes)' --apply"
ssh -o BatchMode=yes -o ConnectTimeout=12 -o ServerAliveInterval=10 -o ServerAliveCountMax=3 $server $cleanup
if ($LASTEXITCODE -ne 0) { throw 'Redundant HDF compaction failed; raw backups retained' }
scp -o BatchMode=yes "${server}:$base/$label-hdf-compaction.json" "$destination/hdf-compaction.json"
if ($LASTEXITCODE -ne 0) { throw 'Compaction record backup failed' }
$traceCleanup="export PYTHONDONTWRITEBYTECODE=1; '$recipes/validation-python/bin/python' '$recipes/compress-verified-formal-traces-20260914.py' '$Module' --local-verified-sha256 '$checksum' --local-verified-bytes '$($raw.bytes)' --apply"
ssh -o BatchMode=yes -o ConnectTimeout=12 -o ServerAliveInterval=10 -o ServerAliveCountMax=3 $server $traceCleanup
if ($LASTEXITCODE -ne 0) { throw 'Trace compression failed; complete raw backups retained' }
scp -o BatchMode=yes "${server}:$base/$label-trace-compression.json" "$destination/trace-compression.json"
if ($LASTEXITCODE -ne 0) { throw 'Trace compression record backup failed' }
Write-Output "S5_FORMAL_PHASE_DOUBLE_BACKUP_PASS $Module $checksum"
