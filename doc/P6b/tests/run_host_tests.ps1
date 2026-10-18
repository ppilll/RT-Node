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
# Reuse the unchanged P6a UART safety tests through a generated adapter in
# P6b evidence only. P6a test source, logs, HEX, MAP and handoff are untouched.
$uart = [IO.File]::ReadAllText((Join-Path $repo 'doc/P6a/tests/uart_host.c'))
$uart = $uart.Replace('../../../App/uart/uart_app.c', ($repo + '/App/uart/uart_app.c')).
    Replace('static HealthBootSnapshot boot;', "static HealthBootSnapshot boot;`nstatic HealthRuntimeSnapshot runtime;").
    Replace('void Health_GetBootSnapshot', "void Health_GetRuntimeSnapshot(HealthRuntimeSnapshot *out) { *out = runtime; }`nvoid Health_GetBootSnapshot").
    Replace('memset(&boot, 0, sizeof(boot));', 'memset(&boot, 0, sizeof(boot)); memset(&runtime, 0, sizeof(runtime));').
    Replace('assert(strstr(reply, "WDG=OFF\r\n") != NULL);', 'assert(strstr(reply, " W=OFF ") != NULL);').
    Replace('puts("P6a UART assertions', 'puts("P6b UART recovery regression assertions')
$extra = @'
    for (reason = 0U; reason <= HEALTH_RESET_OTHER; reason++)
    {
        unsigned c, w;
        for (policy = 0U; policy <= HEALTH_STORE_BAD_LOCKED; policy++)
        for (c = 0U; c <= HEALTH_CTRL_STALLED; c++)
        for (w = 0U; w <= HEALTH_WDG_STARTING; w++)
        {
            Fixture(); boot.reset_reason = (HealthResetReason)reason;
            boot.persistent_state = (HealthPersistentState)policy;
            runtime.control_progress = (HealthControlProgress)c;
            runtime.watchdog = (HealthWatchdogState)w; runtime.test_stop_active = true;
            size = Feed("HEALTH?\n", reply); assert(size <= 58U);
            assert(strstr(reply, " T=1\r\n") != NULL);
            if (c == HEALTH_CTRL_STALLED) { assert(strstr(reply, "C=STALL") != NULL); }
            if (w == HEALTH_WDG_START_ERROR) { assert(strstr(reply, "W=STARTERR") != NULL); }
            if (w == HEALTH_WDG_STARTING) { assert(strstr(reply, "W=START ") != NULL); }
        }
    }
'@
$uart = $uart.Replace('    return 0;', ($extra + "`n    return 0;"))
[IO.File]::WriteAllText((Join-Path $outDir 'uart_host.c'), $uart, [Text.UTF8Encoding]::new($false))
foreach ($case in @('runtime_production','runtime_t2','uart','p6a_stage0','p6a_stage1')) {
    $stage = if ($case -eq 'p6a_stage1') { 1 } else { 0 }
    $src = if ($case -eq 'uart') { Join-Path $outDir 'uart_host.c' }
           elseif ($case.StartsWith('p6a')) { Join-Path $repo 'doc/P6a/tests/health_control_host.c' }
           else { Join-Path $PSScriptRoot 'runtime_host.c' }
    $argsList = @('/nologo','/TC','/std:c11','/utf-8','/Od','/DSTM32F103xE',"/DP3B_OUTPUT_STAGE=$stage",
        "/Fo$outDir/$case.obj", "/Fe$outDir/$case.exe")
    if ($case -eq 'runtime_t2') { $argsList += @('/DP6_WDG_TEST_ENABLE=1','/DP6_WDG_TEST_UNLOADED_ACK=1') }
    foreach ($dir in $includeDirs) { $argsList += "/I$dir" }
    $argsList += @($src,'/link',"/LIBPATH:$msvc/lib/x64",
        "/LIBPATH:$sdk/Lib/10.0.20348.0/ucrt/x64", "/LIBPATH:$sdk/Lib/10.0.20348.0/um/x64")
    $compiled = & $compiler @argsList 2>&1
    $compiled | Set-Content -Encoding utf8 (Join-Path $outDir "compile_$case.log")
    if ($LASTEXITCODE -ne 0) { throw ($compiled -join "`n") }
    $result = & (Join-Path $outDir "$case.exe") 2>&1
    $result | Tee-Object -FilePath (Join-Path $outDir "result_$case.log")
    if ($LASTEXITCODE -ne 0) { throw "Host assertions failed ($case)" }
}
# Expected compilation failures prove test macros cannot leak into output
# builds or retain an acknowledgement in production.
$guards = @(
    @('/DP6_WDG_TEST_ENABLE=1'),
    @('/DP6_WDG_TEST_ENABLE=1','/DP6_WDG_TEST_UNLOADED_ACK=1','/DP3B_OUTPUT_STAGE=1'),
    @('/DP6_WDG_TEST_ENABLE=0','/DP6_WDG_TEST_UNLOADED_ACK=1'),
    @('/DP6_WDG_TEST_ENABLE=2')
)
$records = @()
foreach ($guard in $guards) {
    $argsList = @('/nologo','/TC','/std:c11','/utf-8','/DSTM32F103xE','/c',"/Fo$outDir/guard.obj") + $guard
    foreach ($dir in $includeDirs) { $argsList += "/I$dir" }
    $output = & $compiler @argsList (Join-Path $PSScriptRoot 'runtime_host.c') 2>&1
    if ($LASTEXITCODE -eq 0 -or ($output -join '') -notmatch 'P6[ _]')
    { throw ('Expected P6 configuration guard failure: ' + ($output -join "`n")) }
    $records += [ordered]@{Defines=$guard; ExitCode=$LASTEXITCODE; Output=($output -join "`n"); Expected='Compilation rejection'}
}
$records | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $outDir 'compile_guards.json')
Write-Output 'P6b compile-time test guards PASS; board tests NOT RUN.'
