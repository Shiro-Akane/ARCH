# Continue only after completed BD/RKL1 has been recovered and double-backed-up.
# No retry of a partial experiment, no recurring automation.
$ErrorActionPreference='Stop'
Set-Location -LiteralPath 'C:\tmp\ARCH-perf-20260909'
$env:PYTHONIOENCODING='utf-8'
$env:PYTHONDONTWRITEBYTECODE='1'
$python='C:/Users/jiang/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$modules=@('diffusion_rkl1','diffusion_rkl2','burn_be_nr','burn_bd','burn_ros4',
 'coupled_be_nr_rkl1_all_transport','coupled_be_nr_rkl2_all_transport','coupled_bd_rkl1_all_transport')
$root='validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914'
$receipt=Get-Content -LiteralPath "$root/coupled_bd_rkl1_all_transport/collect-after-reconnect.json" -Raw | ConvertFrom-Json
if ($receipt.status -ne 'completed_existing_worker_collected_without_resampling' -or
    $receipt.original_worker_preserved -ne $true) { throw 'Completed-phase recovery is not finished' }
$audit='build/eight-formals-before-three-phase-resume-20260915.json'
if (Test-Path -LiteralPath $audit) { throw 'Previous continuation attempt exists; inspect before retry' }
$other=Get-CimInstance Win32_Process | Where-Object {
    $_.CommandLine -and $_.ProcessId -ne $PID -and
    ($_.CommandLine -like '*run-remaining-coupled-after-capacity-20260915.ps1*' -or
     $_.CommandLine -like '*run-fixed-formal-phase-v3-20260914.ps1*' -or
     $_.CommandLine -like '*collect-completed-v3-phase-after-tool-reconnect-20260915.ps1*')
}
if ($other) { throw 'Earlier controller/collector still exists' }
& $python build/audit-fixed-formal-storage-20260914.py $root --archives build --modules $modules --output $audit
if ($LASTEXITCODE -ne 0) { throw 'Eight-module scientific/storage qualification failed' }
foreach ($module in @('coupled_bd_rkl2_all_transport','coupled_ros4_rkl1_all_transport','coupled_ros4_rkl2_all_transport')) {
    # The original v3 worker owns empty-output, idle-GPU and 8 GiB start barriers,
    # fixed 3600 s wall, full samples, and unchanged scientific identities.
    & './build/run-fixed-formal-phase-v3-20260914.ps1' -Module $module
}
Write-Output 'S5_LAST_THREE_COUPLED_PHASES_COMPLETE_AFTER_RECONNECT_RECOVERY'
& './build/run-be-followup-after-coupled-20260914.ps1' -CompletedFormals
