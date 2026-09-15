param([Parameter(Mandatory=$true)][string]$Module, [int]$RecoverControllerPid=0)
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
$mode='run'
if ($RecoverControllerPid -gt 0) {
 $mode='collect'
 Write-Output "S5_RECOVER_EXISTING_CONTROLLER $Module pid=$RecoverControllerPid; no duplicate sampling"
 $lastState=''
 while ($true) {
  $probe="state=`$(ps -p $RecoverControllerPid -o stat=); if test -z `"`$state`" || test `"`${state#Z}`" != `"`$state`"; then echo EXITED; else echo RUNNING; grep -c '^TIMING_PASS ' '$base/$label.stdout' || true; fi"
  $state=ssh -o BatchMode=yes -o ConnectTimeout=12 $server $probe
  if ($LASTEXITCODE -ne 0) { Write-Output 'S5_RECOVERY_NETWORK_RETRY; remote computation not restarted'; Start-Sleep -Seconds 30; continue }
  if ($state[0] -eq 'EXITED' -or $state -eq 'EXITED') { break }
  $message=$state -join ' '
  if ($message -ne $lastState) { Write-Output "$Module $message"; $lastState=$message }
  Start-Sleep -Seconds 30
 }
}
# Only after the old controller exits may we upload/start the recovery worker.
$worker='run-fixed-formal-worker-v3-20260914.sh'
scp -o BatchMode=yes "build/$worker" "${server}:$recipes/$worker"
if ($LASTEXITCODE -ne 0) { throw 'Worker upload failed; nothing relaunched' }
$control="$base/controller-v3-$label"
$launch=@'
set -euo pipefail
recipes=__RECIPES__
control=__CONTROL__
mkdir "$control"
bash -n "$recipes/run-fixed-formal-worker-v3-20260914.sh"
nohup bash "$recipes/run-fixed-formal-worker-v3-20260914.sh" __MODULE__ __MODE__ > "$control/worker.log" 2>&1 < /dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'S5_DETACHED_WORKER_PID '; cat "$control/pid"
'@
$launch=$launch.Replace('__RECIPES__',$recipes).Replace('__CONTROL__',$control).Replace('__MODULE__',$Module).Replace('__MODE__',$mode)
ssh -o BatchMode=yes -o ConnectTimeout=12 $server $launch
if ($LASTEXITCODE -ne 0) { throw 'Launch outcome uncertain; inspect controller directory, never blindly relaunch' }
$lastState=''
while ($true) {
 $probe="if test -f '$control/exit-code'; then echo FINISHED; cat '$control/exit-code'; else echo RUNNING; grep -c '^TIMING_PASS ' '$base/$label.stdout' 2>/dev/null || true; fi"
 $state=ssh -o BatchMode=yes -o ConnectTimeout=12 $server $probe
 if ($LASTEXITCODE -ne 0) { Write-Output 'S5_DETACHED_NETWORK_RETRY; worker remains independent'; Start-Sleep -Seconds 30; continue }
 if ($state[0] -eq 'FINISHED') {
  if ($state[1] -ne '0') { throw "Detached worker failed with exit $($state[1]); all results retained" }
  break
 }
 $message=$state -join ' '
 if ($message -ne $lastState) { Write-Output "$Module $message"; $lastState=$message }
 Start-Sleep -Seconds 30
}
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
scp -o BatchMode=yes "${server}:$control/worker.log" "$destination/controller-v3.log"
if ($LASTEXITCODE -ne 0) { throw 'Controller log backup failed' }
Copy-Item -LiteralPath "build/$worker" -Destination "$destination/controller-v3.sh"
Copy-Item -LiteralPath 'build/run-fixed-formal-phase-v3-20260914.ps1' -Destination "$destination/controller-v3.ps1"
Write-Output "S5_FORMAL_PHASE_DOUBLE_BACKUP_PASS $Module $checksum"
