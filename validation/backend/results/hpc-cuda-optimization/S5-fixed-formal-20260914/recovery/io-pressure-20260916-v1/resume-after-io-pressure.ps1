# Fresh complete campaign after the separately archived partial attempt.
# Never combine its 80 completed samples with this retry; original v3 is frozen.
$ErrorActionPreference='Stop'
Set-Location -LiteralPath 'C:\tmp\ARCH-perf-20260909'
$env:PYTHONIOENCODING='utf-8'
$env:PYTHONDONTWRITEBYTECODE='1'
$python='C:/Users/jiang/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$root='validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914'
$recovery="$root/recovery/io-pressure-20260916-v1"
$verified=Get-Content -LiteralPath "$recovery/local-backup-verification.json" -Raw | ConvertFrom-Json
$relocated=Get-Content -LiteralPath "$recovery/relocation.json" -Raw | ConvertFrom-Json
if ($verified.status -ne 'failed_attempt_backup_identity_verified' -or
    $verified.scientific_validation_complete -ne $false -or
    $relocated.status -ne 'failed_attempt_relocated_after_double_backup' -or
    $null -ne $relocated.pending_move -or $relocated.moved.Count -ne 11 -or
    $verified.raw.sha256 -ne $relocated.backup.raw.sha256) { throw 'Incomplete failed-attempt preservation' }
$frozen='bb8d31abf2c83bb8007f563665354e292ff26219bf81938e9145be1923df2479'
if ((Get-FileHash -LiteralPath build/run-fixed-formal-phase-v3-20260914.ps1 -Algorithm SHA256).Hash.ToLower() -ne $frozen) {
    throw 'Original formal controller changed'
}
$others=Get-CimInstance Win32_Process | Where-Object {
    $_.CommandLine -and $_.ProcessId -ne $PID -and
    ($_.CommandLine -like '*run-fixed-formal-phase-v3-20260914.ps1*' -or
     $_.CommandLine -like '*resume-three-coupled-after-reconnect-20260915.ps1*' -or
     $_.CommandLine -like '*resume-after-io-pressure.ps1*')
}
if ($others) { throw 'Another local controller still exists' }
$audit="$recovery/pre-retry-nine-module-audit.json"
if (Test-Path -LiteralPath $audit) { throw 'Retry request already exists; inspect, do not repeat' }
$modules=@('diffusion_rkl1','diffusion_rkl2','burn_be_nr','burn_bd','burn_ros4',
 'coupled_be_nr_rkl1_all_transport','coupled_be_nr_rkl2_all_transport',
 'coupled_bd_rkl1_all_transport','coupled_bd_rkl2_all_transport')
$result=& $python build/audit-fixed-formal-storage-20260914.py $root --archives build --modules $modules --output $audit
if ($LASTEXITCODE -ne 0) { $result | Write-Output; throw 'Nine-module evidence verification failed' }
Write-Output 'NINE_QUALIFIED_MODULES_REVERIFIED_BEFORE_FRESH_ROS_CAMPAIGN'
$check=@'
import json, pathlib, shutil, sys
from datetime import datetime, timezone
sys.path.insert(0, '/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/ros4-rkl1-io-pressure-20260916-v1')
import preserve_failed_formal as p
p.quiescent()
for module in ('coupled_ros4_rkl1_all_transport', 'coupled_ros4_rkl2_all_transport'):
    label='formal-'+module+'-v1'
    p.require(not (p.BASE/label).exists() and not (p.BASE/('controller-v3-'+label)).exists(), 'Existing run/controller; no overwrite')
p.require(not (pathlib.Path('/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/factor-cache/long-be-extended-wall-v1')).exists(), 'Original BE followup already exists')
raw=pathlib.Path('/proc/pressure/io').read_text()
line=next(v for v in raw.splitlines() if v.startswith('full '))
fields=dict(v.split('=') for v in line.split()[1:])
p.require(float(fields['avg10']) <= 1.0 and float(fields['avg60']) <= 1.0, 'System I/O pressure not quiet; no samples started')
free=shutil.disk_usage(p.ROOT).free
p.require(free >= 8*1024**3, 'Below original 8 GiB start requirement')
print(json.dumps(dict(status='pre_retry_quiescence_passed', utc=datetime.now(timezone.utc).isoformat(),
    io_pressure=raw, free_bytes=free, extra_quiet_check_percent=1.0, original_runtime_guard_unchanged=True,
    partial_attempt_preserved=True, fresh_complete_campaign=True)))
'@
$check | ssh -o BatchMode=yes -o ConnectTimeout=12 ubuntu@100.97.101.5 '/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/validation-python/bin/python -B -' |
    Tee-Object -FilePath "$recovery/pre-retry-quiescence.log"
if ($LASTEXITCODE -ne 0) { throw 'Pre-retry quiet barrier failed; no new campaign started' }
Write-Output 'FRESH_ROS_CAMPAIGN_AFTER_PRESERVED_IO_PRESSURE_FAILURE; no samples reused'
foreach ($module in @('coupled_ros4_rkl1_all_transport','coupled_ros4_rkl2_all_transport')) {
    & './build/run-fixed-formal-phase-v3-20260914.ps1' -Module $module
}
Write-Output 'S5_REMAINING_ROS_PHASES_COMPLETE_AFTER_PRESERVED_IO_PRESSURE_FAILURE'
& './build/run-be-followup-after-coupled-20260914.ps1' -CompletedFormals
