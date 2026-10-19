param([string]$Uv4 = 'D:\MKD5.36\UV4\UV4.exe')
$ErrorActionPreference = 'Stop'
if (Get-Process UV4 -ErrorAction SilentlyContinue) { throw 'Close Keil before isolated Rebuild.' }
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$variant = 'stage0'
$prefix = 'P7_STAGE0'
$destination = Join-Path $PSScriptRoot "local_results/$variant"
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$armcc = Join-Path (Split-Path (Split-Path $Uv4 -Parent) -Parent) 'ARM/ARMCC/bin/armcc.exe'
if (!(Test-Path -LiteralPath $Uv4) -or !(Test-Path -LiteralPath $armcc)) {
    @{Status='NOT_RUN'; Reason='Keil5/ARMCC5 missing'; Hardware='NOT_RUN'} | ConvertTo-Json |
        Set-Content -Encoding utf8 (Join-Path $destination 'build.json')
    throw 'Keil5/ARMCC5 missing: NOT_RUN'
}
$source = [IO.File]::ReadAllText((Join-Path $repo 'Projects/RTnode.uvprojx'))
[xml]$project = $source
$defines = $project.Project.Targets.Target.TargetOption.TargetArmAds.Cads.VariousControls.Define
if ($defines -match 'P3B_|P6_WDG_' -or $defines -notmatch 'P1B_AUTOTEST_ENABLE=0')
{ throw 'Expected production project without output/test overrides.' }
# Formal Stage0 only. No download/debug commands or build hooks executed.
$testDefines = ',P6_WDG_TEST_ENABLE=0,P6_WDG_TEST_UNLOADED_ACK=0,P2B_OUTPUT_STAGE=0'
$buildDefines = $defines + ',P3B_OUTPUT_STAGE=0' + $testDefines
$buildSource = $source.Replace('..\', ($repo + '\')).
    Replace('<Define>' + $defines + '</Define>', '<Define>' + $buildDefines + '</Define>').
    Replace('<OutputName>RTnode</OutputName>', '<OutputName>' + $prefix + '</OutputName>')
# Reject user hooks instead of allowing a local build to perform external IO.
if ($buildSource -match '<RunUserProg[12]>1</RunUserProg[12]>') { throw 'Build hooks must be disabled.' }
[IO.File]::WriteAllText((Join-Path $destination 'build_project.uvprojx'), $buildSource, [Text.UTF8Encoding]::new($false))
& $armcc --vsn 2>&1 |
    Set-Content -Encoding utf8 (Join-Path $destination 'compiler_version.txt')
$started = [DateTimeOffset]::UtcNow
$process = Start-Process -FilePath $Uv4 -WorkingDirectory $destination -WindowStyle Hidden `
    -ArgumentList @('-r', 'build_project.uvprojx', '-t', '"Target 1"', '-o', 'rebuild.log') -PassThru -Wait
$logPath = Join-Path $destination 'rebuild.log'
$log = [IO.File]::ReadAllText($logPath)
$hex = Join-Path $destination ($prefix + '.hex')
$passed = $process.ExitCode -eq 0 -and $log -match '0 Error\(s\), 0 Warning\(s\)' -and
    (Test-Path -LiteralPath $hex) -and (Get-Item -LiteralPath $hex).LastWriteTimeUtc -ge $started.UtcDateTime
[ordered]@{
    StartedUtc=$started.ToString('o'); EndedUtc=[DateTimeOffset]::UtcNow.ToString('o');
    ExitCode=$process.ExitCode; BuildPass=$passed; SourceHead=(git -C $repo rev-parse HEAD);
    WorkingTree='P7 working tree; source SHA256 manifest in source_hashes.json'; Variant=$variant; Defines=$buildDefines;
    Download='NOT RUN'; Hardware='NOT RUN'; SafetyPage='0x0807F800 reserved; no download'
} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $destination 'build.json')
Write-Output ($log -split "`n" | Where-Object { $_ -match 'Program Size:|Error\(s\)|error:|warning:|creating hex' })
if (!$passed) { throw 'Rebuild failed/warned or fresh HEX missing; inspect rebuild.log.' }
$hexResult = & python (Join-Path $repo 'doc/P6a/check_hex.py') $hex
if ($LASTEXITCODE -ne 0) { throw 'HEX safety page check failed.' }
$hexResult | Tee-Object -FilePath (Join-Path $destination 'hex_check.txt')

$files = Get-ChildItem -LiteralPath (Join-Path $repo 'App'),(Join-Path $repo 'Projects') -Recurse -File | Where-Object { $_.Extension -in '.c','.h','.uvprojx' }
$files | ForEach-Object { [ordered]@{ Path=$_.FullName.Substring($repo.Length+1); SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash } } | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $destination 'source_hashes.json')
Get-FileHash -LiteralPath $hex -Algorithm SHA256 | Format-List | Out-String | Set-Content -Encoding utf8 (Join-Path $destination 'hex_sha256.txt')
& python (Join-Path $PSScriptRoot 'scripts/check_build.py') --directory $destination --armcc $armcc
if ($LASTEXITCODE -ne 0) { throw 'Effective macros/MAP/Stage0 path/HEX verification failed.' }
