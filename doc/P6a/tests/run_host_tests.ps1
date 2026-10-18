$ErrorActionPreference = 'Stop'
$compiler = 'D:/VS_IDE/VC/Tools/MSVC/14.42.34433/bin/Hostx64/x64/cl.exe'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$msvc = 'D:/VS_IDE/VC/Tools/MSVC/14.42.34433'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$outDir = Join-Path $PSScriptRoot '../evidence/host'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$includeDirs = @("$PSScriptRoot/stubs", "$repo/doc/P5b/tests/stubs", "$msvc/include",
    "$sdk/Include/10.0.20348.0/ucrt", "$sdk/Include/10.0.20348.0/um", "$sdk/Include/10.0.20348.0/shared",
    "$repo/App/imu/include", "$repo/App/actuator/include", "$repo/App/env", "$repo/App/can", "$repo/App/uart", "$repo/App/adc")
foreach ($case in @('stage0','stage1','uart')) {
    $stage = if ($case -eq 'stage1') { 1 } else { 0 }
    $sourceName = if ($case -eq 'uart') { 'uart_host.c' } else { 'health_control_host.c' }
    $argsList = @('/nologo','/TC','/std:c11','/utf-8','/Od','/DSTM32F103xE',"/DP3B_OUTPUT_STAGE=$stage",
        "/Fo$outDir/$case.obj", "/Fe$outDir/$case.exe")
    foreach ($dir in $includeDirs) { $argsList += "/I$dir" }
    $argsList += @("$PSScriptRoot/$sourceName",'/link',"/LIBPATH:$msvc/lib/x64",
        "/LIBPATH:$sdk/Lib/10.0.20348.0/ucrt/x64", "/LIBPATH:$sdk/Lib/10.0.20348.0/um/x64")
    $compiled = & $compiler @argsList 2>&1
    $compiled | Set-Content -Encoding utf8 (Join-Path $outDir "compile_$case.log")
    if ($LASTEXITCODE -ne 0) { throw ($compiled -join "`n") }
    $result = & (Join-Path $outDir "$case.exe") 2>&1
    $result | Tee-Object -FilePath (Join-Path $outDir "result_$case.log")
    if ($LASTEXITCODE -ne 0) { throw "Host assertions failed ($case)" }
}
