param([string]$Uv4 = 'D:\MKD5.36\UV4\UV4.exe')
$ErrorActionPreference = 'Stop'
if (Get-Process UV4 -ErrorAction SilentlyContinue) { throw 'Close Keil before isolated Rebuild.' }
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$destination = Join-Path $PSScriptRoot 'evidence/stage0'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$source = [IO.File]::ReadAllText((Join-Path $repo 'Projects/RTnode.uvprojx'))
[xml]$project = $source
$defines = $project.Project.Targets.Target.TargetOption.TargetArmAds.Cads.VariousControls.Define
if ($defines -match 'P3B_OUTPUT_STAGE|P3B_LOADED_APPROVED' -or
    $defines -notmatch 'P1B_AUTOTEST_ENABLE=0') { throw 'Expected default production Stage0 project.' }
# The copy uses absolute source/include paths; production project/outputs stay untouched.
$buildSource = $source.Replace('..\', ($repo + '\')).
    Replace('<Define>' + $defines + '</Define>', '<Define>' + $defines + ',P3B_OUTPUT_STAGE=0</Define>').
    Replace('<OutputName>RTnode</OutputName>', '<OutputName>P6a_STAGE0</OutputName>')
$copy = Join-Path $destination 'build_project.uvprojx'
[IO.File]::WriteAllText($copy, $buildSource, [Text.UTF8Encoding]::new($false))
& 'D:\MKD5.36\ARM\ARMCC\bin\armcc.exe' --vsn 2>&1 |
    Set-Content -Encoding utf8 (Join-Path $destination 'compiler_version.txt')
$started = [DateTimeOffset]::UtcNow
$process = Start-Process -FilePath $Uv4 -WorkingDirectory $destination -WindowStyle Hidden `
    -ArgumentList @('-r', 'build_project.uvprojx', '-t', '"Target 1"', '-o', 'rebuild.log') -PassThru -Wait
$logPath = Join-Path $destination 'rebuild.log'
$log = [IO.File]::ReadAllText($logPath)
$hex = Join-Path $destination 'P6a_STAGE0.hex'
$passed = $process.ExitCode -eq 0 -and $log -match '0 Error\(s\), 0 Warning\(s\)' -and
    (Test-Path -LiteralPath $hex) -and (Get-Item -LiteralPath $hex).LastWriteTimeUtc -ge $started.UtcDateTime
[ordered]@{
    StartedUtc=$started.ToString('o'); EndedUtc=[DateTimeOffset]::UtcNow.ToString('o');
    ExitCode=$process.ExitCode; BuildPass=$passed; SourceHead=(git -C $repo rev-parse HEAD);
    WorkingTree='P6a uncommitted changes'; Stage=0; Download='NOT RUN'; Hardware='NOT RUN'
} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $destination 'build.json')
Write-Output ($log -split "`n" | Where-Object { $_ -match 'Program Size:|Error\(s\)|error:|warning:|creating hex' })
if (!$passed) { throw 'Rebuild failed/warned or fresh HEX missing; inspect rebuild.log.' }
$hexResult = & python (Join-Path $PSScriptRoot 'check_hex.py') $hex
if ($LASTEXITCODE -ne 0) { throw 'HEX safety page check failed.' }
$hexResult | Tee-Object -FilePath (Join-Path $destination 'hex_check.txt')
