param(
    [string]$Uv4 = 'D:\MKD5.36\UV4\UV4.exe'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$projectDir = Join-Path $repo 'Projects'
$destination = Join-Path $PSScriptRoot 'evidence/armcc'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
if (Get-Process UV4 -ErrorAction SilentlyContinue) { throw 'Keil is already open; close it before Rebuild.' }
[xml]$project = Get-Content -Raw -LiteralPath (Join-Path $projectDir 'RTnode.uvprojx')
$defines = $project.Project.Targets.Target.TargetOption.TargetArmAds.Cads.VariousControls.Define
if ($defines -notmatch 'P1B_AUTOTEST_ENABLE=0' -or $defines -match 'P1B_TEST_ARM_KEY|P1B_UNLOADED_ACK' -or
    $defines -match 'P1A_(AUTOTEST|ACTUATOR_TEST)_ENABLE=1') { throw 'Expected unarmed P2a project.' }
# Preserve pre-existing project outputs/preferences byte for byte. The tested
# firmware is delivered ONLY in evidence/armcc; no download or debug action.
$saved = @{}
Get-ChildItem -LiteralPath $projectDir -Recurse -File | ForEach-Object {
    $saved[$_.FullName] = [IO.File]::ReadAllBytes($_.FullName)
}
try {
    $started = Get-Date
    $process = Start-Process -FilePath $Uv4 -WorkingDirectory $projectDir -WindowStyle Hidden `
        -ArgumentList @('-r', 'RTnode.uvprojx', '-t', '"Target 1"', '-o', '..\doc\P2a\evidence\armcc\rebuild.log') `
        -PassThru -Wait
    $log = Get-Content -Raw -LiteralPath (Join-Path $destination 'rebuild.log')
    [pscustomobject]@{Started=$started.ToString('o'); Ended=(Get-Date).ToString('o');
        ExitCode=$process.ExitCode; Defines=$defines; Project='Projects/RTnode.uvprojx';
        Target='Target 1'; Action='Rebuild; no download'; OutputsRestored=$true} |
        ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $destination 'build.json')
    if ($process.ExitCode -ne 0 -or $log -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        throw 'ARMCC Rebuild failed/warned; see evidence/armcc/rebuild.log'
    }
    foreach ($file in @('RTnode.axf','RTnode.hex','RTnode.build_log.htm','RTnode.lnp','RTnode.sct','RTnode.htm')) {
        Copy-Item -LiteralPath (Join-Path $projectDir $file) -Destination $destination
    }
    Copy-Item -LiteralPath (Join-Path $projectDir 'Listings/RTnode.map') -Destination $destination
    Get-ChildItem -LiteralPath $destination -File | Where-Object Name -in @('RTnode.axf','RTnode.hex','RTnode.map') |
        Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash |
        ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $destination 'hashes.json')
    Write-Output ($log -split "`n" | Where-Object { $_ -match 'Program Size:|Error\(s\)' })
}
finally {
    foreach ($path in $saved.Keys) { [IO.File]::WriteAllBytes($path, $saved[$path]) }
}
