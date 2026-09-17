$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../../..')).Path
Set-Location -LiteralPath $repo
$server = 'ubuntu@100.97.101.5'
$root = '/home/ubuntu/projects/ARCH-native-wave-v4-20260916'
$python = 'C:/Users/jiang/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$remotePython = '/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914/validation-python/bin/python'
$expected = '116611b7470ea7e8717f349edbfcbf8cff5614fac45e10f9368b8224f4bbec30'
$env:PYTHONIOENCODING = 'utf-8'
$env:PYTHONDONTWRITEBYTECODE = '1'
function Remote([string]$command) {
    & ssh -o BatchMode=yes -o ConnectTimeout=12 -o ServerAliveInterval=10 -o ServerAliveCountMax=3 $server $command
    if ($LASTEXITCODE -ne 0) { throw 'Remote step failed; do not replay mutations or remove partial evidence' }
}
function Observe([string]$command) {
    # Retries are strictly limited to read-only observations, never dispatch.
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try { return @(Remote $command) }
        catch {
            if ($attempt -eq 3) { throw }
            Write-Warning "Read-only observation unavailable; server worker untouched ($attempt/3)."
            Start-Sleep -Seconds 20
        }
    }
}
$parent = Get-Content -Raw -LiteralPath 'build/window-focused-v1-download/window-focused-collection-v1.json' | ConvertFrom-Json
$receipt = Get-Content -Raw -LiteralPath 'build/window-focused-v1-verified/window-focused-local-receipt-v1.json' | ConvertFrom-Json
if ($parent.trajectory_matrix_pass -ne $true -or $parent.worker_exit_code -ne 0 -or
    $receipt.status -ne 'both_archives_and_all_members_byte_verified') { throw 'Prior focused qualification/backup missing' }
$payload = 'build/window-capacity-prepared-v1/window-capacity-input-v1.tar'
if ((Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'Frozen capacity payload changed' }
Remote "grep -qx 0 '$root/window-focused-control-v1/exit-code' && test -s '$root/window-focused-local-receipt-v1.json' && test ! -e '$root/window-capacity-input-v1.tar' && test ! -e '$root/window-capacity-input-v1' && test ! -e '$root/window-capacity-control-v1' && test ! -e '$root/window-capacity-v1' && ! pgrep -x nvcc && ! pgrep -x ptxas && ! pgrep -x cc1plus && ! pgrep -x ARCH"
if ((@(Remote 'nvidia-smi --query-compute-apps=pid --format=csv,noheader') -join '').Trim()) { throw 'GPU is not idle' }
& scp -o BatchMode=yes -o ConnectTimeout=15 $payload "${server}:$root/window-capacity-input-v1.tar"
if ($LASTEXITCODE -ne 0) { throw 'Upload uncertain; inspect remotely, never automatically redispatch' }
$identity = @(Remote "sha256sum '$root/window-capacity-input-v1.tar'")
if ($identity.Count -ne 1 -or -not $identity[0].StartsWith($expected + '  ')) { throw 'Uploaded payload SHA mismatch' }
Remote "tar -xf '$root/window-capacity-input-v1.tar' -C '$root' && bash '$root/window-capacity-input-v1/capacity-dispatch.sh'"
$pidText = @(Remote "cat '$root/window-capacity-control-v1/pid'")
if ($pidText.Count -ne 1 -or $pidText[0] -notmatch '^[0-9]+$') { throw 'Unexpected worker identity' }
$worker = [int]$pidText[0]
$control = "$root/window-capacity-control-v1"
Write-Output "CAPACITY_CONTINUATION local_pid=$PID remote_pid=$worker"
$previous = ''
$finished = $false
for ($tick = 0; $tick -lt 1900; $tick++) {
    $state = @(Observe "if test -f '$control/exit-code'; then echo FINISHED; cat '$control/exit-code'; else ps -p $worker -o comm= | grep -qx bash || exit 4; echo RUNNING; test ! -f '$control/window-capacity-child.log' || tail -n 1 '$control/window-capacity-child.log'; fi")
    if ($state.Count -eq 2 -and $state[0] -eq 'FINISHED') {
        Write-Output "WINDOW_CAPACITY_EXIT $($state[1])"
        Observe "tail -n 8 '$control/window-capacity-guard.log'; tail -n 6 '$control/window-capacity-child.log'"
        $finished = $true
        break
    }
    if ($state.Count -lt 1 -or $state.Count -gt 2 -or $state[0] -ne 'RUNNING') { throw 'Unexpected worker state' }
    $stage = if ($state.Count -eq 2) { $state[1] } else { '' }
    if ($stage -ne $previous -or ($tick % 12) -eq 0) {
        Write-Output "$([DateTime]::UtcNow.ToString('o')) capacity $stage"
        $previous = $stage
    }
    Start-Sleep -Seconds 50
}
if (-not $finished) { throw 'Observation bound reached; server worker untouched' }
Remote "PYTHONDONTWRITEBYTECODE=1 '$remotePython' '$root/window-capacity-input-v1/collect_capacity.py'"
$download = 'build/window-capacity-v1-download'
$verified = 'build/window-capacity-v1-verified'
$projection = 'validation/network/results/native-wave-20260917/window-capacity-v1'
foreach ($path in @($download, $verified, $projection)) {
    if (Test-Path -LiteralPath $path) { throw "Preserve existing path: $path" }
}
New-Item -ItemType Directory -Path $download | Out-Null
& scp -o BatchMode=yes -o ConnectTimeout=15 "${server}:$root/window-capacity-raw-v1.tar.zst" "${server}:$root/window-capacity-compact-v1.tar.zst" "${server}:$root/window-capacity-collection-v1.json" $download
if ($LASTEXITCODE -ne 0) { throw 'Archive download failed; preserve partial evidence' }
& $python validation/network/native-wave-candidate/verify_factory_download.py --download $download --output $verified --receipt-name window-capacity-collection-v1.json --local-receipt-name window-capacity-local-receipt-v1.json
if ($LASTEXITCODE -ne 0) { throw 'Archive verification failed' }
Copy-Item -LiteralPath "$verified/compact/compact" -Destination $projection -Recurse
Copy-Item -LiteralPath "$download/window-capacity-collection-v1.json", "$verified/window-capacity-local-receipt-v1.json" -Destination $projection
& scp -o BatchMode=yes -o ConnectTimeout=15 "$verified/window-capacity-local-receipt-v1.json" "${server}:$root/window-capacity-local-receipt-v1.json"
if ($LASTEXITCODE -ne 0) { throw 'Receipt upload failed' }
Write-Output 'WINDOW_CAPACITY_DOUBLE_BACKUP_COMPLETE'
