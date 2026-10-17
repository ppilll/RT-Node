param(
    [ValidateSet(0,1,2)][int]$Stage = 0,
    [switch]$G2EvidenceAck,
    [switch]$OperatorAck,
    [string]$Uv4 = 'D:\MKD5.36\UV4\UV4.exe'
)
$ErrorActionPreference = 'Stop'
if ($Stage -eq 2) {
    if (!$G2EvidenceAck -or !$OperatorAck) {
        throw 'Stage2 requires reviewed G2 evidence and explicit operator approval (-G2EvidenceAck -OperatorAck).'
    }
} elseif ($G2EvidenceAck -or $OperatorAck) {
    throw 'G2/operator acknowledgements belong only to Stage2.'
}
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$projectDir = Join-Path $repo 'Projects'
$projectPath = Join-Path $projectDir 'RTnode.uvprojx'
$destination = Join-Path $PSScriptRoot "evidence/armcc/stage$Stage"
New-Item -ItemType Directory -Force -Path $destination | Out-Null
if (Get-Process UV4 -ErrorAction SilentlyContinue) { throw 'Close Keil before Rebuild.' }
$source = [IO.File]::ReadAllText($projectPath)
[xml]$project = $source
$defines = $project.Project.Targets.Target.TargetOption.TargetArmAds.Cads.VariousControls.Define
if ($defines -notmatch 'P1B_AUTOTEST_ENABLE=0' -or $defines -match 'P2B_|P1B_TEST_ARM_KEY|P1B_UNLOADED_ACK' -or
    $defines -match 'P1A_(AUTOTEST|ACTUATOR_TEST)_ENABLE=1') { throw 'Expected default Stage0 project.' }
if ($Stage -eq 1) {
    $buildDefines = $defines + ',P2B_OUTPUT_STAGE=1,P2B_UNLOADED_ACK=1,P2B_OUTPUT_ARM_KEY=0xB2201500UL'
} elseif ($Stage -eq 2) {
    $buildDefines = $defines + ',P2B_OUTPUT_STAGE=2,P2B_G2_EVIDENCE_ACK=1,P2B_OPERATOR_ACK=1,P2B_OUTPUT_ARM_KEY=0xB2201500UL'
} else { $buildDefines = $defines }
$saved = @{}
Get-ChildItem -LiteralPath $projectDir -Recurse -File | ForEach-Object {
    $saved[$_.FullName] = [IO.File]::ReadAllBytes($_.FullName)
}
try {
    [IO.File]::WriteAllText($projectPath, $source.Replace("<Define>$defines</Define>", "<Define>$buildDefines</Define>"),
                          [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath $projectPath -Destination (Join-Path $destination 'build_project.uvprojx')
    & 'D:\MKD5.36\ARM\ARMCC\bin\armcc.exe' --vsn 2>&1 |
        Set-Content -Encoding utf8 (Join-Path $destination 'compiler_version.txt')
    $started = Get-Date
    $process = Start-Process -FilePath $Uv4 -WorkingDirectory $projectDir -WindowStyle Hidden `
        -ArgumentList @('-r', 'RTnode.uvprojx', '-t', '"Target 1"', '-o', (Join-Path $destination 'rebuild.log')) `
        -PassThru -Wait
    $log = Get-Content -Raw -LiteralPath (Join-Path $destination 'rebuild.log')
    [pscustomobject]@{Started=$started.ToString('o'); Ended=(Get-Date).ToString('o');
        ExitCode=$process.ExitCode; Defines=$buildDefines; Stage=$Stage; Project='Projects/RTnode.uvprojx';
        Target='Target 1'; Action='Keil Rebuild only; no download'; OutputsRestored=$true;
        PhysicalKeys='NOT_INDEPENDENTLY_VERIFIED';
        G1Hardware=$(if ($Stage -eq 2) { 'USER_REPORTED_PASS' } else { 'NOT_REPORTED_BY_THIS_BUILD' });
        G2=$(if ($Stage -eq 2) { 'OPERATOR_ACKNOWLEDGED_PASS' } else { 'NOT_APPROVED_BY_THIS_BUILD' });
        G2EvidenceAck=[bool]$G2EvidenceAck; OperatorAck=[bool]$OperatorAck;
        HardwareRunByCodex='NOT_RUN'; SourceHead=(git -C $repo rev-parse HEAD)} |
        ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $destination 'build.json')
    if ($process.ExitCode -ne 0 -or $log -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        throw 'ARMCC Rebuild failed/warned; see rebuild.log'
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
