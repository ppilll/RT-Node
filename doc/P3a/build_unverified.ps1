param(
    [switch]$UnverifiedBuildOnly,
    [string]$Uv4 = 'D:\MKD5.36\UV4\UV4.exe'
)
$ErrorActionPreference = 'Stop'
if (!$UnverifiedBuildOnly) { throw 'Explicit -UnverifiedBuildOnly required; this is not a T1 firmware build.' }
if (Get-Process UV4 -ErrorAction SilentlyContinue) { throw 'Close Keil before this isolated Rebuild.' }
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$projectDir = Join-Path $repo 'Projects'
$destination = Join-Path $PSScriptRoot 'evidence/armcc_unverified'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$projectPath = Join-Path $projectDir 'RTnode.uvprojx'
$source = [IO.File]::ReadAllText($projectPath)
[xml]$project = $source
$target = $project.Project.Targets.Target
$controls = $target.TargetOption.TargetArmAds.Cads.VariousControls
if ($controls.Define -match 'P2B_|P3A_|P1B_TEST_ARM_KEY|P1B_UNLOADED_ACK' -or
    $controls.Define -notmatch 'P1B_AUTOTEST_ENABLE=0' -or
    $controls.Define -match 'P1A_(AUTOTEST|ACTUATOR_TEST)_ENABLE=1') { throw 'Expected default Stage0 project, with no CAN timing overrides.' }
# ARITHMETIC CANDIDATE ONLY, NOT HISTORICAL/MEASURED: 36MHz/(4*(1+14+3))
# = 500000 bit/s, sample point=(1+14)/18=83.3333%, SJW=1tq.
# All candidate values are centralized here and affect only the isolated copy.
$buildDefines = $controls.Define + ',P2B_OUTPUT_STAGE=0,P3A_CAN_TIMING_CONFIRMED=0,P3A_CAN_UNVERIFIED_BUILD_ONLY=1,P3A_CAN_BITRATE=500000UL,P3A_CAN_PRESCALER=4UL,P3A_CAN_BS1_TQ=14UL,P3A_CAN_BS2_TQ=3UL,P3A_CAN_SJW_TQ=1UL'
# Build the existing project identity (which has the installed toolchain's
# working options); temporarily redirect its outputs, then restore all bytes.
# The saved build_project is an audit copy, with paths relative to Projects.
$buildSource = $source.Replace("<Define>$($controls.Define)</Define>", "<Define>$buildDefines</Define>").
    Replace('<OutputDirectory>.\</OutputDirectory>', '<OutputDirectory>..\doc\P3a\evidence\armcc_unverified\objects\</OutputDirectory>').
    Replace('<ListingPath>.\Listings\</ListingPath>', '<ListingPath>..\doc\P3a\evidence\armcc_unverified\listings\</ListingPath>').
    Replace('<OutputName>RTnode</OutputName>', '<OutputName>P3a_UNVERIFIED_NOT_FOR_FLASH</OutputName>').
    Replace('<CreateHexFile>1</CreateHexFile>', '<CreateHexFile>0</CreateHexFile>')
$buildProject = Join-Path $destination 'build_project.uvprojx'
[IO.File]::WriteAllText($buildProject, $buildSource, [Text.UTF8Encoding]::new($false))
$logPath = Join-Path $destination 'rebuild.log'
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
        -ArgumentList @('-r', 'RTnode.uvprojx', '-t', '"Target 1"', '-o', '..\doc\P3a\evidence\armcc_unverified\rebuild.log') `
        -PassThru -Wait
} finally {
    foreach ($path in $saved.Keys) { [IO.File]::WriteAllBytes($path, $saved[$path]) }
}
# This Keil installation takes its linker map directory from existing options.
$mapPath = Join-Path $projectDir 'Listings/P3a_UNVERIFIED_NOT_FOR_FLASH.map'
if (Test-Path -LiteralPath $mapPath) {
    Move-Item -LiteralPath $mapPath -Destination (Join-Path $destination 'P3a_UNVERIFIED_NOT_FOR_FLASH.map') -Force
}
$log = if (Test-Path -LiteralPath $logPath) { Get-Content -Raw -LiteralPath $logPath } else { 'NO_BUILD_LOG_PRODUCED' }
$passed = $process.ExitCode -eq 0 -and $log -match '0 Error\(s\), 0 Warning\(s\)'
[ordered]@{
    StartedUtc=$started.ToString('o'); EndedUtc=[DateTimeOffset]::UtcNow.ToString('o');
    ExitCode=$process.ExitCode; BuildOnlyPass=$passed; Timing='UNVERIFIED_ARITHMETIC_CANDIDATE';
    Defines=$buildDefines; SourceHead=(git -C $repo rev-parse HEAD);
    ProductionTiming='BLOCKED'; T1='NOT_TESTED'; Download='NOT_RUN'; HexGenerated=$false;
    Note='Temporary build macros/output paths restored byte-for-byte; no download or HEX.'
} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $destination 'build.json')
Write-Output ($log -split "`n" | Where-Object { $_ -match 'Program Size:|Error\(s\)|error:|warning:' })
if (!$passed) { throw 'UNVERIFIED build failed or warned; see rebuild.log.' }
