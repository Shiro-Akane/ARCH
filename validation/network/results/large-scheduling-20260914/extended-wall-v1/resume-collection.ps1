# Resume observation/collection ONLY. Never launch or restart server experiments.
$ErrorActionPreference='Stop'
$repo='C:/tmp/ARCH-perf-20260909'
Set-Location -LiteralPath $repo
$server='ubuntu@100.97.101.5'
$root='/home/ubuntu/projects/ARCH-microphysics-20260914'
$base="$root/build/p12-20260914"
$control="$base/be-extended-wall-controller-v1"
$metadata='build/large-be-extended-wall-v1-raw-archive.json'
$rawFile='build/large-be-extended-wall-v1.tar.zst'
$compactFile='build/large-be-extended-wall-compact-v1.tar.zst'
$extract='build/large-be-extended-wall-compact-v1'
$target='validation/network/results/large-scheduling-20260914/extended-wall-v1'
foreach ($path in @($metadata,$rawFile,$compactFile,$extract,$target)) {
 if (Test-Path -LiteralPath $path) { throw "Existing/partial collection must be inspected, not overwritten: $path" }
}
Write-Output "BE_EXISTING_WORKER_RESUME_COLLECTION_ONLY local_pid=$PID"
$tick=0
while ($true) {
 $probe="test `"`$(cat '$control/pid')`" = 131631 || exit 3; if test -f '$control/exit-code'; then echo FINISHED; cat '$control/exit-code'; else ps -p 131631 -o comm= | grep -qx bash || exit 4; echo RUNNING; fi"
 $state=@(ssh -o BatchMode=yes -o ConnectTimeout=12 -o ServerAliveInterval=10 -o ServerAliveCountMax=3 $server $probe)
 if ($LASTEXITCODE -ne 0) {
  Write-Output 'BE_COLLECTION_OBSERVATION_FAILED; originals untouched; no runtime restart'
  throw 'Inspect connection or existing worker; collection did not restart anything'
 }
 if ($state.Count -eq 2 -and $state[0] -eq 'FINISHED') {
  if ($state[1] -ne '0') { throw "Existing worker/archive exit=$($state[1]); inspect preserved originals" }
  break
 }
 if ($state.Count -ne 1 -or $state[0] -ne 'RUNNING') { throw 'Unexpected existing-worker status' }
 if (($tick % 10) -eq 0) {
  & './build/probe-be-extended-wall-20260916.ps1'
  if ($LASTEXITCODE -ne 0) { throw 'Read-only progress probe failed; no work restarted' }
 }
 $tick++
 Start-Sleep -Seconds 30
}
scp -o BatchMode=yes "${server}:$base/factor-cache/long-be-extended-wall-v1-archive/compact/raw-archive.json" $metadata
if ($LASTEXITCODE -ne 0) { throw 'Raw metadata download failed' }
$raw=Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
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
