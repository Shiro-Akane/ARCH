# Invoke only after the three burn formal phase drivers have exited successfully.
param()
$ErrorActionPreference='Stop'
$repo='C:\tmp\ARCH-perf-20260909'
Set-Location -LiteralPath $repo
$python='C:/Users/jiang/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$env:PYTHONIOENCODING='utf-8'
$env:PYTHONDONTWRITEBYTECODE=1
$destination='validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914'
$modules=@('coupled_be_nr_rkl1_all_transport','coupled_be_nr_rkl2_all_transport',
 'coupled_bd_rkl1_all_transport','coupled_bd_rkl2_all_transport',
 'coupled_ros4_rkl1_all_transport','coupled_ros4_rkl2_all_transport')
foreach($module in $modules) {
 if (Test-Path -LiteralPath "$destination/$module") { throw 'Coupled output already exists; use an explicitly scoped continuation' }
}
& $python 'build/audit-fixed-formal-storage-20260914.py' $destination --archives build --modules burn_be_nr burn_bd burn_ros4
if ($LASTEXITCODE -ne 0) { throw 'Burn phases are not completely qualified and backed up' }
$archive=Get-Item -LiteralPath 'build/fixed-science-v1.tar.zst'
$sha=(Get-FileHash -LiteralPath $archive.FullName -Algorithm SHA256).Hash.ToLower()
if ($archive.Length -ne 1601189714 -or $sha -ne '613405b2b5570bdbb081a95feed5f863b66e699e9afab9a6d1881c79b99abbae') { throw 'Fixed science local backup mismatch' }
$server='ubuntu@100.97.101.5'
$recipes='/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914'
$name='compress-verified-coupled-science-traces-20260914.py'
scp -o BatchMode=yes "build/$name" "${server}:$recipes/$name"
if ($LASTEXITCODE -ne 0) { throw 'Compression recipe upload failed' }
$run=@'
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
recipes=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914
"$recipes/validation-python/bin/python" "$recipes/compress-verified-coupled-science-traces-20260914.py" \
 --local-verified-sha256 613405b2b5570bdbb081a95feed5f863b66e699e9afab9a6d1881c79b99abbae \
 --local-verified-bytes 1601189714 --apply
df -Pk /home/ubuntu/projects
'@
ssh -o BatchMode=yes -o ConnectTimeout=12 -o ServerAliveInterval=10 -o ServerAliveCountMax=3 $server $run
if ($LASTEXITCODE -ne 0) { throw 'Verified science trace compression failed; originals retained' }
New-Item -ItemType Directory -Path "$destination/storage" -Force | Out-Null
scp -o BatchMode=yes "${server}:/home/ubuntu/projects/ARCH-multiphysics-fix-20260914/build/fix-20260914/coupled-preformal-trace-compression-v1.json" "$destination/storage/coupled-preformal-trace-compression-v1.json"
if ($LASTEXITCODE -ne 0) { throw 'Compression ledger download failed' }
Copy-Item -LiteralPath "build/$name" -Destination "$destination/storage"
Copy-Item -LiteralPath 'build/run-fixed-coupled-phases-20260914.ps1' -Destination "$destination/storage"
Write-Output 'S5_COUPLED_STORAGE_PREPARATION_PASS'
foreach($module in $modules) {
 & './build/run-fixed-formal-phase-v2-20260914.ps1' -Module $module
}
Write-Output 'S5_COUPLED_SIX_PHASES_COMPLETE'
