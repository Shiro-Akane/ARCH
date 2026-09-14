# Only project the completed first phase; its original compact and raw remain intact.
$ErrorActionPreference='Stop'
$repo='C:\tmp\ARCH-perf-20260909'
$relative='validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914/diffusion_rkl1'
Set-Location -LiteralPath $repo
if (git ls-files -- $relative) { throw 'Refuse to remove tracked evidence' }
$scope=(Resolve-Path -LiteralPath "$relative/evidence").Path
$original=(Resolve-Path -LiteralPath 'build/fixed-formal-compact-diffusion_rkl1-v1/compact').Path
if (-not $scope.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Wrong workspace scope' }
$raw=(Resolve-Path -LiteralPath build/fixed-formal-diffusion_rkl1-v1.tar.zst).Path
if ((Get-FileHash -LiteralPath $raw -Algorithm SHA256).Hash.ToLower() -ne '13a6a15b48fd984e57b53fd7596ea4b888fcadaadc673cd776a7b305c96c27fc') { throw 'Raw backup mismatch' }
$manifest=Get-Content -LiteralPath "$scope/raw-manifest.json" -Raw | ConvertFrom-Json -AsHashtable
$mapping=Get-Content -LiteralPath "$scope/projection-map.json" -Raw | ConvertFrom-Json -AsHashtable
$output=Join-Path $scope 'git-projection-omissions.json'
if (Test-Path -LiteralPath $output) { throw 'Projection already recorded' }
$omitted=[ordered]@{}
$targets=@(Get-ChildItem -LiteralPath "$scope/samples" -Recurse -File -Filter '*.tsv' | Where-Object {$_.Length -gt 2MB})
foreach ($file in $targets) {
    $parent=$file.Directory
    while ($parent.FullName.Length -ge $scope.Length) {
        if ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse directory in scope' }
        $parent=$parent.Parent
    }
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse target' }
    $name=[IO.Path]::GetRelativePath($scope,$file.FullName).Replace('\','/')
    $source=Join-Path $original $name
    $expected=$manifest[$mapping[$name]]
    $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLower()
    if ($hash -ne $expected.sha256 -or $file.Length -ne $expected.bytes -or
        (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLower() -ne $hash) { throw 'Duplicate trace mismatch' }
    $omitted[$name]=@{original_path=$mapping[$name];bytes=$file.Length;sha256=$hash;available_in='complete raw and original compact archives; original extracted compact retained'}
}
# The manifest is generated evidence. Every target was verified before any removal.
$report=@{policy='Omit TSV files larger than 2 MiB from the Git projection only; raw/original compact and their verified bytes are unchanged';raw_archive=$raw;raw_sha256='13a6a15b48fd984e57b53fd7596ea4b888fcadaadc673cd776a7b305c96c27fc';files=$omitted;removed=@()}
[IO.File]::WriteAllText($output,($report | ConvertTo-Json -Depth 8)+"`n",[Text.UTF8Encoding]::new($false))
try {
    foreach ($file in $targets) {
        $path=[IO.Path]::GetFullPath($file.FullName)
        if (-not $path.StartsWith($scope+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Target escaped workspace scope' }
        $name=[IO.Path]::GetRelativePath($scope,$path).Replace('\','/')
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower() -ne $omitted[$name].sha256) { throw 'Trace changed before removal' }
        Remove-Item -LiteralPath $path
        $report.removed += $name
    }
} finally {
    [IO.File]::WriteAllText($output,($report | ConvertTo-Json -Depth 8)+"`n",[Text.UTF8Encoding]::new($false))
}
Write-Output "VERIFIED_RKL1_GIT_PROJECTION_PASS $($targets.Count)"
