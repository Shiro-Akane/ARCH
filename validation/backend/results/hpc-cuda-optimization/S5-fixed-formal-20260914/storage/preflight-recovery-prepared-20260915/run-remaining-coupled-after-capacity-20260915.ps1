# One-shot continuation AFTER the earlier driver has exited, its never-started
# BD/RKL1 preflight has been preserved, and capacity is safely restored.
# Never use to retry a partially sampled phase. No recurring automation.
$ErrorActionPreference='Stop'
Set-Location -LiteralPath 'C:\tmp\ARCH-perf-20260909'
$env:PYTHONIOENCODING='utf-8'
$env:PYTHONDONTWRITEBYTECODE=1
$python='C:/Users/jiang/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$server='ubuntu@100.97.101.5'
$root='/home/ubuntu/projects/ARCH-multiphysics-fix-20260914'
$base="$root/build/fix-20260914/timing"
$completed=@('diffusion_rkl1','diffusion_rkl2','burn_be_nr','burn_bd','burn_ros4',
 'coupled_be_nr_rkl1_all_transport','coupled_be_nr_rkl2_all_transport')
$result='validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914'
$audit='build/seven-formals-before-capacity-resume-20260915.json'
if (Test-Path -LiteralPath $audit) { throw 'Previous resumption attempt exists; inspect first' }
$old=Get-CimInstance Win32_Process | Where-Object {
 $_.CommandLine -like '*resume-fixed-coupled-phases-20260914.ps1*'
}
if ($old) { throw 'Earlier local controller still exists; no second controller started' }
& $python 'build/audit-fixed-formal-storage-20260914.py' $result --archives build --modules $completed --output $audit
if ($LASTEXITCODE -ne 0) { throw 'Seven completed formal modules are not fully qualified/backed up' }
$preserved='preserved-preflight-coupled_bd_rkl1_all_transport-20260915-v1'
$probe=@'
set -euo pipefail
base=__BASE__
recipes=/home/ubuntu/projects/ARCH-microphysics-20260914/build/p12-20260914
export PYTHONDONTWRITEBYTECODE=1
for name in ARCH nvcc ptxas cc1plus; do
 status=0
 pgrep -x "$name" >/dev/null || status=$?
 if [[ "$status" -ne 1 ]]; then echo "Active $name or failed process check; refusing second workflow" >&2; exit 2; fi
done
status=0
pgrep -f '^/home/ubuntu/projects/.*/arch_cuda_generated_sparse_burn_' >/dev/null || status=$?
if [[ "$status" -ne 1 ]]; then echo 'Sparse harness active or failed process check; refusing second workflow' >&2; exit 2; fi
gpu_processes=$(nvidia-smi --query-compute-apps=pid --format=csv,noheader)
test -z "$gpu_processes"
test "$(df -Pk "$base" | awk 'NR==2 {print $4}')" -ge 8388608
test ! -e "$base/controller-v3-formal-coupled_bd_rkl1_all_transport-v1"
test ! -e "$base/formal-coupled_bd_rkl1_all_transport-v1"
test ! -e "$base/formal-coupled_bd_rkl1_all_transport-v1.stdout"
test ! -e "$base/formal-coupled_bd_rkl1_all_transport-v1.stderr"
"$recipes/validation-python/bin/python" -c 'import hashlib,json,pathlib,sys; sys.flags.optimize and sys.exit("Receipt checks require assertions enabled; refusing optimized Python"); p=pathlib.Path(sys.argv[1]); assert p.is_dir() and not p.is_symlink(); r=json.loads((p/"preservation.json").read_text()); assert r["status"]=="preserved_never_started_preflight" and r["sample_output_absent"] and r["scientific_validation"] is False and r["module"]=="coupled_bd_rkl1_all_transport" and r["pending_move"] is None; assert r["before"] and all(not pathlib.PurePosixPath(k).is_absolute() and ".." not in pathlib.PurePosixPath(k).parts and "\\" not in k and not (p/k).is_symlink() and (p/k).resolve().is_relative_to(p.resolve()) and (p/k).is_file() and (p/k).stat().st_size==v["bytes"] and hashlib.sha256((p/k).read_bytes()).hexdigest()==v["sha256"] for k,v in r["before"].items()); print("PRESERVED_PREFLIGHT_IDENTITY_PASS_NOT_SCIENCE_PASS")' "$base/__PRESERVED__"
printf 'CAPACITY_RESUME_BARRIER_PASS\n'
'@
$probe=$probe.Replace('__BASE__',$base).Replace('__PRESERVED__',$preserved)
ssh -o BatchMode=yes -o ConnectTimeout=12 $server $probe
if ($LASTEXITCODE -ne 0) { throw 'Capacity/recovery/idle barrier failed; no remaining phase launched' }
foreach ($module in @('coupled_bd_rkl1_all_transport','coupled_bd_rkl2_all_transport',
 'coupled_ros4_rkl1_all_transport','coupled_ros4_rkl2_all_transport')) {
 & './build/run-fixed-formal-phase-v3-20260914.ps1' -Module $module
}
Write-Output 'S5_REMAINING_FOUR_COUPLED_PHASES_COMPLETE_AFTER_CAPACITY_BARRIER'
& './build/run-be-followup-after-coupled-20260914.ps1' -CompletedFormals
