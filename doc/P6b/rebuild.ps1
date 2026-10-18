param([switch]$T2, [string]$Uv4 = 'D:\MKD5.36\UV4\UV4.exe')
$ErrorActionPreference = 'Stop'
if (Get-Process UV4 -ErrorAction SilentlyContinue) { throw 'Close Keil before isolated Rebuild.' }
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$variant = if ($T2) { 't2_stage0' } else { 'production_stage0' }
$prefix = if ($T2) { 'P6b_T2_STAGE0' } else { 'P6b_PRODUCTION_STAGE0' }
$destination = Join-Path $PSScriptRoot "evidence/$variant"
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$source = [IO.File]::ReadAllText((Join-Path $repo 'Projects/RTnode.uvprojx'))
[xml]$project = $source
$defines = $project.Project.Targets.Target.TargetOption.TargetArmAds.Cads.VariousControls.Define
if ($defines -match 'P3B_|P6_WDG_' -or $defines -notmatch 'P1B_AUTOTEST_ENABLE=0')
{ throw 'Expected production project without output/test overrides.' }
# Compile only, no download or board interaction. T2 execution requires the
# user to physically disconnect servo power/signals before loading the HEX.
$testDefines = if ($T2) { ',P6_WDG_TEST_ENABLE=1,P6_WDG_TEST_UNLOADED_ACK=1' }
               else { ',P6_WDG_TEST_ENABLE=0,P6_WDG_TEST_UNLOADED_ACK=0' }
$buildDefines = $defines + ',P3B_OUTPUT_STAGE=0' + $testDefines
$buildSource = $source.Replace('..\', ($repo + '\')).
    Replace('<Define>' + $defines + '</Define>', '<Define>' + $buildDefines + '</Define>').
    Replace('<OutputName>RTnode</OutputName>', '<OutputName>' + $prefix + '</OutputName>')
[IO.File]::WriteAllText((Join-Path $destination 'build_project.uvprojx'), $buildSource, [Text.UTF8Encoding]::new($false))
& 'D:\MKD5.36\ARM\ARMCC\bin\armcc.exe' --vsn 2>&1 |
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
    WorkingTree='P6a + P6b uncommitted changes'; Variant=$variant; Defines=$buildDefines;
    Download='NOT RUN'; Hardware='NOT RUN'; P6aEvidence='Unchanged by request'
} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $destination 'build.json')
Write-Output ($log -split "`n" | Where-Object { $_ -match 'Program Size:|Error\(s\)|error:|warning:|creating hex' })
if (!$passed) { throw 'Rebuild failed/warned or fresh HEX missing; inspect rebuild.log.' }
$hexResult = & python (Join-Path $repo 'doc/P6a/check_hex.py') $hex
if ($LASTEXITCODE -ne 0) { throw 'HEX safety page check failed.' }
$hexResult | Tee-Object -FilePath (Join-Path $destination 'hex_check.txt')
