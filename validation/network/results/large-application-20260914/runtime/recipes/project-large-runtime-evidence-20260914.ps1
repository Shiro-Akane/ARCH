$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath('C:\tmp\ARCH-perf-20260909')
$source = [IO.Path]::GetFullPath((Join-Path $repo 'validation/network/results/large-application-20260914/runtime'))
$retained = [IO.Path]::GetFullPath((Join-Path $repo 'build/large-runtime-unflattened-compact-20260914'))
foreach ($target in @($source, $retained)) {
    if (-not $target.StartsWith($repo + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Out-of-scope evidence path: $target"
    }
}
if (Test-Path -LiteralPath $retained) { throw 'Retained compact destination already exists' }
if ((git -C $repo ls-files -- 'validation/network/results/large-application-20260914/runtime').Count -gt 0) {
    throw 'Refuse to move tracked evidence'
}
$record = Get-Content -LiteralPath (Join-Path $source 'raw-archive.json') -Raw -Encoding utf8 | ConvertFrom-Json
if ($record.sha256 -ne '297de8027483181d63f7a975f910b68f63aa6da5d5260b85fdc743e5a51c0569') {
    throw 'Unexpected compact identity'
}
# Both resolved absolute targets were checked above. Retain every original byte;
# only project a shallower Git layout so Windows users do not need long paths.
Move-Item -LiteralPath $source -Destination $retained
New-Item -ItemType Directory -Path $source | Out-Null
$app = Join-Path $retained 'files/ARCH-large-application-20260914'
$base = Join-Path $app 'build/p12-20260914/large-application-v2'
$runtime = Join-Path $base 'runtime-v1'
foreach ($name in @('raw-archive.json','raw-manifest.json','additional-trajectory-quality.json')) {
    Copy-Item -LiteralPath (Join-Path $retained $name) -Destination $source
}
Copy-Item -LiteralPath (Join-Path $runtime 'backend-validation-evidence.json') -Destination $source
$cases = Join-Path $source 'cases'
New-Item -ItemType Directory -Path $cases | Out-Null
foreach ($case in Get-ChildItem -LiteralPath $runtime -Directory) {
    Copy-Item -LiteralPath $case.FullName -Destination $cases -Recurse
}
$environment = Join-Path $source 'environment'
New-Item -ItemType Directory -Path $environment | Out-Null
Get-ChildItem -LiteralPath $base -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $environment
}
Copy-Item -LiteralPath (Join-Path $retained 'files/ARCH-microphysics-20260914/build/p12-20260914') -Destination (Join-Path $source 'recipes') -Recurse
$long = @(Get-ChildItem -LiteralPath $source -File -Recurse | Where-Object { $_.FullName.Length -ge 260 })
if ($long.Count) { throw "Projected evidence still contains $($long.Count) long paths" }
Write-Output 'LARGE_RUNTIME_SHALLOW_PROJECTION_PASS_ORIGINAL_RETAINED'
