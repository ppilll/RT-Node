$ErrorActionPreference = 'Stop'
# MSVC native assertions; generated EXEs live only in the OS temp directory.
$compiler = 'D:/VS_IDE/VC/Tools/MSVC/14.42.34433/bin/Hostx64/x64/cl.exe'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$msvc = 'D:/VS_IDE/VC/Tools/MSVC/14.42.34433'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$outDir = Join-Path ([IO.Path]::GetTempPath()) ('RTNode-P5b-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outDir | Out-Null
$includeDirs = @("$PSScriptRoot/stubs", "$msvc/include", "$sdk/Include/10.0.20348.0/ucrt",
    "$repo/App/imu/include", "$repo/App/actuator/include", "$repo/App/env", "$repo/App/can", "$repo/App/uart", "$repo/App/adc")
foreach ($stage in @(0,1)) {
    $argsList = @('/nologo','/TC','/std:c11','/utf-8','/Od',"/DP3B_OUTPUT_STAGE=$stage", "/Fo$outDir/stage$stage.obj", "/Fe$outDir/stage$stage.exe")
    foreach ($dir in $includeDirs) { $argsList += "/I$dir" }
    $argsList += @("$PSScriptRoot/control_display_host.c",'/link',"/LIBPATH:$msvc/lib/x64", "/LIBPATH:$sdk/Lib/10.0.20348.0/ucrt/x64", "/LIBPATH:$sdk/Lib/10.0.20348.0/um/x64")
    & $compiler @argsList
    if ($LASTEXITCODE -ne 0) { throw "Host compile failed (stage $stage)" }
    & (Join-Path $outDir "stage$stage.exe")
    if ($LASTEXITCODE -ne 0) { throw "Host assertions failed (stage $stage)" }
}
Write-Output "Temporary native test binaries: $outDir"
