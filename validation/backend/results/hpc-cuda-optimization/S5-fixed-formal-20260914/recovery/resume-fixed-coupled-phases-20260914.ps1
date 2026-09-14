# Resume the still-running first module after an SSH connection reset.
# This is a one-shot continuation, not a restart of completed/active samples.
$ErrorActionPreference='Stop'
Set-Location -LiteralPath 'C:\tmp\ARCH-perf-20260909'
$env:PYTHONIOENCODING='utf-8'
$env:PYTHONDONTWRITEBYTECODE=1
& './build/run-fixed-formal-phase-v3-20260914.ps1' -Module coupled_be_nr_rkl1_all_transport -RecoverControllerPid 3660158
foreach ($module in @('coupled_be_nr_rkl2_all_transport',
 'coupled_bd_rkl1_all_transport','coupled_bd_rkl2_all_transport',
 'coupled_ros4_rkl1_all_transport','coupled_ros4_rkl2_all_transport')) {
 & './build/run-fixed-formal-phase-v3-20260914.ps1' -Module $module
}
Write-Output 'S5_COUPLED_SIX_PHASES_COMPLETE_AFTER_CONNECTION_RECOVERY'
& './build/run-be-followup-after-coupled-20260914.ps1' -CompletedFormals
