param(
    [string]$Uv4 = 'D:\MKD5.36\UV4\UV4.exe',
    [string]$FromElf = 'D:\MKD5.36\ARM\ARMCC\Bin\fromelf.exe'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$projectDir = Join-Path $repo 'Projects'
$projectFile = Join-Path $projectDir 'RTnode.uvprojx'
$evidenceDir = Join-Path $PSScriptRoot 'evidence/C2'
if (Get-Process UV4 -ErrorAction SilentlyContinue) {
    throw 'Close the existing Keil process before changing test build flags.'
}
$originalBytes = [IO.File]::ReadAllBytes($projectFile)
$originalText = [Text.Encoding]::UTF8.GetString($originalBytes)
[xml]$projectXml = $originalText
if (@($projectXml.Project.Targets.Target).Count -ne 1) { throw 'Expected one target.' }
$baseDefines = $projectXml.Project.Targets.Target.TargetOption.TargetArmAds.Cads.VariousControls.Define
if ($baseDefines -notmatch 'P1B_AUTOTEST_ENABLE=0' -or
    $baseDefines -match 'P1B_TEST_ARM_KEY|P1B_UNLOADED_ACK|P1B_AUTOTEST_CASE') {
    throw 'Expected the default unarmed C2 project flags.'
}
if ($baseDefines -match 'P1A_(AUTOTEST|ACTUATOR_TEST)_ENABLE=1') { throw 'P1a test must be OFF.' }

function Build-Image([string]$image, [string]$defines) {
    $destination = Join-Path $evidenceDir $image
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $logRelative = "..\doc\P1b\evidence\C2\$image\rebuild.log"
    $started = Get-Date
    $process = Start-Process -FilePath $Uv4 -WorkingDirectory $projectDir -WindowStyle Hidden `
        -ArgumentList @('-r', 'RTnode.uvprojx', '-t', '"Target 1"', '-o', $logRelative) `
        -PassThru -Wait
    $ended = Get-Date
    [pscustomobject]@{Image=$image; Started=$started.ToString('o'); Ended=$ended.ToString('o');
        ExitCode=$process.ExitCode; Defines=$defines; Project='Projects/RTnode.uvprojx';
        Target='Target 1'; Action='Rebuild only; no download'} |
        ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $destination 'build.json')
    $log = Get-Content -Raw -LiteralPath (Join-Path $destination 'rebuild.log')
    if ($process.ExitCode -ne 0 -or $log -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        throw "Build failed/warned: $image; see its rebuild.log"
    }
    foreach ($file in @('RTnode.axf','RTnode.hex','RTnode.build_log.htm','RTnode.lnp','RTnode.sct')) {
        Copy-Item -LiteralPath (Join-Path $projectDir $file) -Destination $destination
    }
    Copy-Item -LiteralPath (Join-Path $projectDir 'Listings/RTnode.map') -Destination $destination
    & $FromElf --bin --output (Join-Path $destination 'RTnode.bin') (Join-Path $destination 'RTnode.axf')
    if ($LASTEXITCODE -ne 0) { throw "fromelf failed: $image" }
    Get-ChildItem -LiteralPath $destination -File | Where-Object Name -in @('RTnode.axf','RTnode.hex','RTnode.bin','RTnode.map') |
        Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash |
        ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $destination 'hashes.json')
    Write-Output "$image : exit=$($process.ExitCode), 0 errors / 0 warnings"
    Write-Output ($log -split "`n" | Where-Object { $_ -match 'Program Size:' })
}

$testFailure = $null
try {
    foreach ($case in 1..4) {
        $defines = $baseDefines.Replace('P1B_AUTOTEST_ENABLE=0', 'P1B_AUTOTEST_ENABLE=1') +
            ",P1B_AUTOTEST_CASE=$case,P1B_UNLOADED_ACK=1,P1B_TEST_ARM_KEY=0xC2201500UL"
        $text = $originalText.Replace("<Define>$baseDefines</Define>", "<Define>$defines</Define>")
        [IO.File]::WriteAllText($projectFile, $text, [Text.UTF8Encoding]::new($false))
        Build-Image "case$case" $defines
    }
}
catch { $testFailure = $_ }
finally {
    # Restore the EXACT unarmed project, even after build failure/interruption.
    [IO.File]::WriteAllBytes($projectFile, $originalBytes)
}
# Leave both the project AND its ordinary Projects/RTnode.hex output unarmed.
Build-Image 'default' $baseDefines
if ($testFailure) { throw $testFailure }
Write-Output 'All five images built. Original single project restored; default output is unarmed.'
