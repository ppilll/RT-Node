param([ValidateSet(0,1)][int]$Stage = 0, [string]$Uv4 = 'D:\MKD5.36\UV4\UV4.exe')
$ErrorActionPreference = 'Stop'
if (Get-Process UV4 -ErrorAction SilentlyContinue) { throw 'Close Keil before Rebuild.' }
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$projectDir = Join-Path $repo 'Projects'
$projectPath = Join-Path $projectDir 'RTnode.uvprojx'
$destination = Join-Path $PSScriptRoot "evidence/stage$Stage"
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$source = [IO.File]::ReadAllText($projectPath)
[xml]$project = $source
$defines = $project.Project.Targets.Target.TargetOption.TargetArmAds.Cads.VariousControls.Define
if ($defines -match 'P2B_|P3B_|P3A_|P1B_TEST_ARM_KEY|P1B_UNLOADED_ACK' -or
    $defines -notmatch 'P1B_AUTOTEST_ENABLE=0' -or
    $defines -notmatch 'P1A_ACTUATOR_TEST_ENABLE=0' -or
    $defines -notmatch 'P1A_AUTOTEST_ENABLE=0') { throw 'Expected default Stage0 project with no timing overrides.' }
# Timing comes only from can_timing.h. Do not mark measured confirmation or
# use the historical UNVERIFIED build-only override for this validation image.
$buildDefines = $defines + ",P3B_OUTPUT_STAGE=$Stage"
$prefix = "P3b_CAN500k_STAGE${Stage}_T2_T4"
$buildSource = $source.Replace("<Define>$defines</Define>", "<Define>$buildDefines</Define>").
    Replace('<OutputDirectory>.\</OutputDirectory>', "<OutputDirectory>..\doc\P3b\evidence\stage$Stage\</OutputDirectory>").
    Replace('<OutputName>RTnode</OutputName>', "<OutputName>$prefix</OutputName>")
if ($buildSource -notmatch '<CreateHexFile>1</CreateHexFile>') { throw 'HEX output must be enabled.' }
[IO.File]::WriteAllText((Join-Path $destination 'build_project.uvprojx'), $buildSource, [Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $repo 'App/can/can_timing.h') -Destination (Join-Path $destination 'can_timing.h')
& 'D:\MKD5.36\ARM\ARMCC\bin\armcc.exe' --vsn 2>&1 |
    Set-Content -Encoding utf8 (Join-Path $destination 'compiler_version.txt')
$saved = @{}
Get-ChildItem -LiteralPath $projectDir -Recurse -File | ForEach-Object {
    $saved[$_.FullName] = [IO.File]::ReadAllBytes($_.FullName)
}
$started = [DateTimeOffset]::UtcNow
try {
    [IO.File]::WriteAllText($projectPath, $buildSource, [Text.UTF8Encoding]::new($false))
    $process = Start-Process -FilePath $Uv4 -WorkingDirectory $projectDir -WindowStyle Hidden `
        -ArgumentList @('-r', 'RTnode.uvprojx', '-t', '"Target 1"', '-o', "..\doc\P3b\evidence\stage$Stage\rebuild.log") `
        -PassThru -Wait
} finally {
    foreach ($path in $saved.Keys) { [IO.File]::WriteAllBytes($path, $saved[$path]) }
}
$mapPath = Join-Path $projectDir "Listings/$prefix.map"
if (Test-Path -LiteralPath $mapPath) { Move-Item -LiteralPath $mapPath -Destination (Join-Path $destination "$prefix.map") -Force }
$logPath = Join-Path $destination 'rebuild.log'
$log = if (Test-Path -LiteralPath $logPath) { Get-Content -Raw -LiteralPath $logPath } else { 'NO_BUILD_LOG_PRODUCED' }
$hexPath = Join-Path $destination "$prefix.hex"
$passed = $process.ExitCode -eq 0 -and $log -match '0 Error\(s\), 0 Warning\(s\)' -and (Test-Path -LiteralPath $hexPath) -and
    (Get-Item -LiteralPath $hexPath).LastWriteTimeUtc -ge $started.UtcDateTime -and
    (Get-Item -LiteralPath $logPath).LastWriteTimeUtc -ge $started.UtcDateTime
[ordered]@{
    StartedUtc=$started.ToString('o'); EndedUtc=[DateTimeOffset]::UtcNow.ToString('o');
    ExitCode=$process.ExitCode; BuildPass=$passed; Defines=$buildDefines;
    Stage=$Stage; Timing='P3A_UNCHANGED_USER_REPORTED_PASS'; TimingSource='App/can/can_timing.h';
    SourceHead=(git -C $repo rev-parse HEAD); P3aT1='USER_REPORTED_PASS'; T2='NOT_TESTED'; T3='NOT_TESTED'; T4='NOT_TESTED'; Download='NOT_RUN';
    Hex=$(if ($passed) { "$prefix.hex" } else { $null });
    HexSHA256=$(if ($passed) { (Get-FileHash -LiteralPath $hexPath -Algorithm SHA256).Hash } else { $null });
    Note='Stage1 requires servo power AND signals disconnected. Stage2 NOT_AUTHORIZED. Project/existing outputs restored; no download performed.'
} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $destination 'build.json')
Write-Output ($log -split "`n" | Where-Object { $_ -match 'Program Size:|Error\(s\)|error:|warning:|creating hex' })
if (!$passed) { throw 'Validation Rebuild failed/warned or HEX missing; see evidence/stage directory.' }
Get-FileHash -LiteralPath $hexPath -Algorithm SHA256 | Format-List
