# One local continuation of the current workflow; no recurring automation.
# No server access until the exact existing coupled controller exits and all
# eleven formal matrices AND their double-backup receipts pass the audit.
param([Parameter(Mandatory=$true,ParameterSetName='Waiting')][int]$CoupledProcessId,
      [Parameter(Mandatory=$true,ParameterSetName='Waiting')][string]$CoupledStartUtc,
      [Parameter(Mandatory=$true,ParameterSetName='Completed')][switch]$CompletedFormals)
$ErrorActionPreference='Stop'
$repo='C:\tmp\ARCH-perf-20260909'
Set-Location -LiteralPath $repo
$python='C:/Users/jiang/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$env:PYTHONIOENCODING='utf-8'
$env:PYTHONDONTWRITEBYTECODE=1
$controller=$null
if (-not $CompletedFormals) {
 $controller=Get-Process -Id $CoupledProcessId
 if ($controller.StartTime.ToUniversalTime().ToString('o') -ne $CoupledStartUtc) {
  throw 'Coupled controller identity does not match; no work started'
 }
 $command=(Get-CimInstance Win32_Process -Filter "ProcessId=$CoupledProcessId").CommandLine
 if ($command -notlike '*run-fixed-coupled-phases-20260914.ps1*') {
  throw 'Not the known coupled phase controller'
 }
}
$stage=Join-Path $repo 'build/be-extended-wall-recipes-v1'
if (Test-Path -LiteralPath $stage) { throw 'Recipe stage already exists; inspect before any continuation' }
New-Item -ItemType Directory -Path "$stage/validation/network","$stage/tests/tooling" | Out-Null
Copy-Item -LiteralPath 'validation/network/run_sparse_capacity.py' -Destination "$stage/validation/network"
Copy-Item -LiteralPath 'tests/tooling/test_sparse_capacity_recipe.py' -Destination "$stage/tests/tooling"
foreach($name in @('replay-large-be-extended-wall-20260914.sh',
 'large-be-extended-wall-protocol-20260914.json','archive-large-be-extended-wall-20260914.py',
 'run-large-be-followup-worker-20260914.sh',
 'sparse-capacity-wall-budget-tests-20260914.log','sparse-capacity-wall-budget-tests-v2-20260914.log')) {
 Copy-Item -LiteralPath "build/$name" -Destination $stage
}
& $python -m unittest discover -s "$stage/tests/tooling" -p test_sparse_capacity_recipe.py -v
if ($LASTEXITCODE -ne 0) { throw 'Staged recipe tests failed' }
if ($controller) {
 Write-Output "BE_FOLLOWUP_WAITING_FOR_COUPLED_CONTROLLER $CoupledProcessId"
 # A managed job wait, not a timing sleep or a competing server task.
 $controller.WaitForExit()
}
$destination='validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914'
& $python 'build/audit-fixed-formal-storage-20260914.py' $destination --archives build --output "$destination/integrity-audit-all-pre-be.json"
if ($LASTEXITCODE -ne 0) { throw 'All formal phases are not qualified; follow-up not started' }
Write-Output 'BE_FOLLOWUP_FORMAL_BARRIER_PASS'
$server='ubuntu@100.97.101.5'
$root='/home/ubuntu/projects/ARCH-microphysics-20260914'
$base="$root/build/p12-20260914"
$recipes="$base/be-extended-wall-recipes-v1"
tar -cf 'build/be-extended-wall-recipes-v1.tar' -C build be-extended-wall-recipes-v1
if ($LASTEXITCODE -ne 0) { throw 'Recipe package failed' }
scp -o BatchMode=yes 'build/be-extended-wall-recipes-v1.tar' "${server}:$base/be-extended-wall-recipes-v1.tar"
if ($LASTEXITCODE -ne 0) { throw 'Recipe upload failed' }
$run=@'
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
root=/home/ubuntu/projects/ARCH-microphysics-20260914
base="$root/build/p12-20260914"
recipes="$base/be-extended-wall-recipes-v1"
python="$base/validation-python/bin/python"
test ! -e "$recipes"
tar -xf "$base/be-extended-wall-recipes-v1.tar" -C "$base"
control="$base/be-extended-wall-controller-v1"
mkdir "$control"
bash -n "$recipes/run-large-be-followup-worker-20260914.sh"
nohup bash "$recipes/run-large-be-followup-worker-20260914.sh" > "$control/worker.log" 2>&1 < /dev/null &
printf '%s\n' "$!" > "$control/pid"
printf 'BE_FOLLOWUP_DETACHED_WORKER_PID '; cat "$control/pid"
'@
ssh -o BatchMode=yes -o ConnectTimeout=12 -o ServerAliveInterval=10 -o ServerAliveCountMax=3 $server $run
if ($LASTEXITCODE -ne 0) { throw 'Follow-up launch outcome uncertain; inspect controller, never blindly relaunch' }
$control="$base/be-extended-wall-controller-v1"
while ($true) {
 $probe="if test -f '$control/exit-code'; then echo FINISHED; cat '$control/exit-code'; else echo RUNNING; fi"
 $state=ssh -o BatchMode=yes -o ConnectTimeout=12 $server $probe
 if ($LASTEXITCODE -ne 0) { Write-Output 'BE_FOLLOWUP_NETWORK_RETRY; no runtime restart'; Start-Sleep -Seconds 30; continue }
 if ($state[0] -eq 'FINISHED') {
  if ($state[1] -ne '0') { throw "BE worker/archive failed with exit $($state[1]); originals retained" }
  break
 }
 Start-Sleep -Seconds 30
}
$metadata='build/large-be-extended-wall-v1-raw-archive.json'
scp -o BatchMode=yes "${server}:$base/factor-cache/long-be-extended-wall-v1-archive/compact/raw-archive.json" $metadata
if ($LASTEXITCODE -ne 0) { throw 'Raw metadata download failed' }
$raw=Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
$rawFile='build/large-be-extended-wall-v1.tar.zst'
$compactFile='build/large-be-extended-wall-compact-v1.tar.zst'
scp -o BatchMode=yes "${server}:$root/build/large-be-extended-wall-v1.tar.zst" $rawFile
if ($LASTEXITCODE -ne 0) { throw 'Raw download failed' }
if ((Get-Item -LiteralPath $rawFile).Length -ne $raw.bytes -or
 (Get-FileHash -LiteralPath $rawFile -Algorithm SHA256).Hash.ToLower() -ne $raw.sha256) {
 throw 'Raw backup mismatch'
}
scp -o BatchMode=yes "${server}:$root/build/large-be-extended-wall-compact-v1.tar.zst" $compactFile
if ($LASTEXITCODE -ne 0) { throw 'Compact download failed' }
$remoteHash=ssh -o BatchMode=yes $server "sha256sum '$root/build/large-be-extended-wall-compact-v1.tar.zst'"
if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $compactFile -Algorithm SHA256).Hash.ToLower() -ne ($remoteHash -split '\s+')[0]) {
 throw 'Compact backup mismatch'
}
$extract='build/large-be-extended-wall-compact-v1'
$target='validation/network/results/large-scheduling-20260914/extended-wall-v1'
if ((Test-Path -LiteralPath $extract) -or (Test-Path -LiteralPath $target)) { throw 'Fresh extraction destination required' }
New-Item -ItemType Directory -Path $extract | Out-Null
tar -xf $compactFile -C $extract
if ($LASTEXITCODE -ne 0) { throw 'Compact extraction failed' }
Copy-Item -LiteralPath "$extract/compact" -Destination $target -Recurse
scp -o BatchMode=yes "${server}:$control/worker.log" "$target/controller.log"
if ($LASTEXITCODE -ne 0) { throw 'BE controller log backup failed' }
scp -o BatchMode=yes "${server}:$base/factor-cache/long-be-extended-wall-v1-collection.log" "$target/collection.log"
if ($LASTEXITCODE -ne 0) { throw 'BE collection log backup failed' }
Write-Output "BE_FOLLOWUP_DOUBLE_BACKUP_COMPLETE scientific_status=$($raw.scientific_status) full_matrix=$($raw.complete_matrix) raw_sha256=$($raw.sha256)"
if ($raw.scientific_status -ne 'passed' -or -not $raw.complete_matrix) {
 throw 'Extended-wall BE numerical matrix did not pass; failure evidence backed up, inspect next'
}
Write-Output 'BE_FOLLOWUP_ORIGINAL_PHYSICS_MATRIX_PASS_NOT_FORMAL_SPEEDUP'
