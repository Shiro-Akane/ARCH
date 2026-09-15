# One-time recovery: collect an already successful independent v3 worker.
# No worker upload/launch, no repeated sampling, no overwrite of partial results.
param([Parameter(Mandatory=$true)][string]$Module)
$ErrorActionPreference='Stop'
$repo='C:\tmp\ARCH-perf-20260909'
Set-Location -LiteralPath $repo
if ($Module -ne 'coupled_bd_rkl1_all_transport') { throw 'This recovery is bound to the observed completed phase' }
$server='ubuntu@100.97.101.5'
$root='/home/ubuntu/projects/ARCH-multiphysics-fix-20260914'
$recipes='/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914'
$base="$root/build/fix-20260914/timing"
$label="formal-$Module-v1"
$stem="fixed-formal-$Module-v1"
$destination=Join-Path $repo "validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914/$Module"
$extract=Join-Path $repo "build/fixed-formal-compact-$Module-v1"
$control="$base/controller-v3-$label"
$worker='run-fixed-formal-worker-v3-20260914.sh'
$driver='build/run-fixed-formal-phase-v3-20260914.ps1'
$expected='bb8d31abf2c83bb8007f563665354e292ff26219bf81938e9145be1923df2479'
if ((Get-FileHash -LiteralPath $driver -Algorithm SHA256).Hash.ToLower() -ne $expected) { throw 'Original collector changed; do not fuzzy-recover' }
foreach ($path in @($destination,$extract,"build/$stem-raw-archive.json","build/$stem.tar.zst","build/$stem-compact.tar.zst")) {
    if (Test-Path -LiteralPath $path) { throw "Existing recovery output requires inspection: $path" }
}
$active=Get-CimInstance Win32_Process | Where-Object {
    $_.CommandLine -and $_.ProcessId -ne $PID -and
    ($_.CommandLine -like '*run-remaining-coupled-after-capacity-20260915.ps1*' -or
     $_.CommandLine -like '*run-fixed-formal-phase-v3-20260914.ps1*')
}
if ($active) { throw 'Another local phase controller exists; no parallel collector started' }
$probe=@'
set -euo pipefail
control=__CONTROL__
base=__BASE__
module=__MODULE__
for name in ARCH nvcc ptxas cc1plus; do
 status=0
 pgrep -x "$name" >/dev/null || status=$?
 test "$status" -eq 1
done
test -z "$(nvidia-smi --query-compute-apps=pid --format=csv,noheader)"
test "$(cat "$control/exit-code")" = 0
pid=$(cat "$control/pid")
state=$(ps -p "$pid" -o stat= || true)
test -z "$state" || test "${state#Z}" != "$state"
grep -Fx "S5_FORMAL_PHASE_ARCHIVE_PASS $module" "$control/worker.log"
grep -Fx "FORMAL_PHASE_ARCHIVE_PASS $module" "$base/collection-$module-v1.log"
test ! -e "$base/formal-$module-v1-hdf-compaction.json"
test ! -e "$base/formal-$module-v1-trace-compression.json"
date -u
stat -c '%y %n' "$control/exit-code"
printf 'COMPLETED_V3_COLLECT_ONLY_BARRIER_PASS\n'
'@
$probe=$probe.Replace('__CONTROL__',$control).Replace('__BASE__',$base).Replace('__MODULE__',$Module)
$observed=ssh -o BatchMode=yes -o ConnectTimeout=12 $server $probe
if ($LASTEXITCODE -ne 0) { throw 'Completed/idle/no-prior-cleanup barrier failed; nothing restarted' }
$observed | Write-Output
# Reuse the exact verified original collection/backup/compaction suffix. It
# cannot enter the earlier mkdir/nohup/sampling path because that prefix is not
# in this script block. All variables above match the original collector.
$text=Get-Content -LiteralPath $driver -Raw
$anchor='scp -o BatchMode=yes "${server}:$base/$label-archive/compact/raw-archive.json" "build/$stem-raw-archive.json"'
$index=$text.IndexOf($anchor,[StringComparison]::Ordinal)
if ($index -lt 0 -or $text.LastIndexOf($anchor,[StringComparison]::Ordinal) -ne $index) { throw 'Original collection boundary is ambiguous' }
& ([ScriptBlock]::Create($text.Substring($index)))
Copy-Item -LiteralPath $PSCommandPath -Destination "$destination/collect-after-reconnect.ps1" -ErrorAction Stop
$record=[ordered]@{
    status='completed_existing_worker_collected_without_resampling'; module=$Module;
    original_driver_sha256=$expected; previous_tool_session='84240';
    tool_session_reconnected_at_utc='2026-09-15T15:30:51Z';
    original_worker_preserved=$true; subsequent_phases_had_not_started=$true;
    observed_barrier=$observed; raw_archive=[ordered]@{bytes=55978168;sha256='6bd1508b392609e1a6e90002b0abb9d150450a9dafb5ded7449c0b62fae4870f'}
}
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$destination/collect-after-reconnect.json" -Encoding utf8
Write-Output 'COMPLETED_V3_COLLECT_ONLY_DOUBLE_BACKUP_PASS'
