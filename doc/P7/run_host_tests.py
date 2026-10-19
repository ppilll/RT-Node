"""Reuses actual P2b/P3b/P6 C fixtures; native simulation, never board IO.

Outputs stay under ignored local_results/. Stage0 firmware only. Historical
P3b GPIO expectations are adapted to current KEY0 STOP / KEY1 display policy.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'doc/P7/local_results/host'
OUT.mkdir(parents=True, exist_ok=True)
records = []
if (OUT / 'results.json').exists():
    (OUT / 'results.json').rename(OUT / ('attempt_' + str(time.time_ns()) + '.json'))
setup_path = os.environ.get('P7_VCVARS', r'D:\VS_IDE\VC\Auxiliary\Build\vcvars64.bat')
setup = subprocess.run(['cmd.exe', '/d', '/c', 'call ' + subprocess.list2cmdline([setup_path]) + ' >nul && set'],
                       capture_output=True, text=True, errors='replace', check=True)
env = {k.upper(): v for k, v in os.environ.items()}
for line in setup.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key.upper()] = value


def run(args, expected=True, message=None):
    result = subprocess.run(args, executable=shutil.which(args[0], path=env['PATH']), cwd=OUT,
                            env=env, capture_output=True, text=True, errors='replace')
    records.append(dict(command=args, expected_success=expected, returncode=result.returncode,
                        stdout=result.stdout, stderr=result.stderr))
    (OUT / 'results.json').write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding='utf8')
    if (result.returncode == 0) != expected or (message and message not in result.stdout + result.stderr):
        raise RuntimeError(result.stdout + result.stderr)
    if args[0] != 'cl.exe':
        print(result.stdout.strip())


def paths(*files):
    return [str(ROOT / file) for file in files]


def compile_run(name, files, includes, defines=(), cases=((),), strict=False):
    flags = ['cl.exe', '/nologo', '/TC', '/std:c11', '/utf-8', '/Od']
    if strict:
        flags += ['/W4', '/WX']
    flags += ['/DSTM32F103xE', '/DP3B_OUTPUT_STAGE=0'] + list(defines)
    flags += ['/I' + str(ROOT / path) for path in includes]
    flags += [str(file) for file in files] + ['/Fe' + str(OUT / (name + '.exe'))]
    run(flags)
    for case in cases:
        run([str(OUT / (name + '.exe')), *case])


policy, mapping = paths('App/control/control_policy.c', 'App/control/local_follow.c')
old_inc = ['doc/P2b/tests/stubs', 'doc/P1b/tests/stubs', 'App/control', 'App/actuator/include', 'App/imu/include']
for sign in (1, -1):
    compile_run('mapping' + str(sign), [policy, mapping, *paths('doc/P2b/tests/local_test.c')], old_inc,
                [f'/DP2B_PAN_SIGN={sign}', f'/DP2B_TILT_SIGN={sign}'], strict=True)
compile_run('policy', [policy, *paths('doc/P1b/tests/policy_test.c')], old_inc, strict=True)
compile_run('fusion', paths('App/imu/src/imu_fusion.c', 'Driver/imu/icm20602_data.c', 'doc/P2a/tests/fusion_test.c'),
            ['doc/P2a/tests/stubs', 'App/imu/include', 'Driver/imu'], strict=True)

# Keep the historical P3b scenarios, update only fixture GPIO/health API and
# extend terminal fault simulation past warmup to exercise actual task loop.
task = (ROOT / 'doc/P3b/tests/controller_test.c').read_text(encoding='utf8')
task = task.replace('gpio->Pin == (GPIO_PIN_3 | GPIO_PIN_4)', 'gpio->Pin == GPIO_PIN_4')
a = task.index('    if (pin == GPIO_PIN_4)')
b = task.index('    return down ?', a)
task = task[:a] + '''    assert(pin == GPIO_PIN_4);
    down = Is("boot_hold") || (Is("switch") && t >= 720U);
''' + task[b:]
task = task.replace('Is("limits") ? 5200U : 500U', 'Is("limits") ? 5200U : Is("fault") ? 12200U : 500U')
task = task.replace('    Inject();', '''    if (Is("fault") && Age() == 10400U)
    { tick += 2U; host_dwt.CYCCNT += 144000U; }
    Inject();''')
task = task.replace('    printf("HOST PASS', '''    if (Is("fault"))
    {
        P7ControlMetrics p; ControlDiagnostics d; uint32_t v;
        Control_GetP7Metrics(&p, &v); Control_GetDiagnostics(&d);
        assert(p.window_start_tick == 10000U && p.period_samples >= 100U);
        assert(p.flags == 1U && p.invalid_samples == 0U && v >= 1U);
        assert(d.late_count == 1U && d.fault == CONTROL_FAULT_DEADLINE);
        assert(p.elapsed_max_cycles >= 40U);
    }
    printf("HOST PASS''')
task += '\nbool Health_ControlAuthorizationAllowed(void) { return true; }\nbool Health_BootAutoLocalAllowed(void) { return true; }\n'
(OUT / 'p3b_task.c').write_text(task, encoding='utf8')
compile_run('p3b_task', [*paths('App/control/control.c'), policy, mapping, OUT / 'p3b_task.c'], old_inc,
            cases=[(x,) for x in ('nominal', 'busy', 'duplicate', 'mode_errors', 'stop', 'timeout', 'overflow',
                                 'invalid', 'remote_imu_failed', 'rollover', 'switch', 'local', 'boot_hold',
                                 'fault', 'local_fault', 'limits')])

inc = ['doc/P6b/tests/stubs', 'doc/P5b/tests/stubs', 'App/imu/include', 'App/actuator/include',
       'App/env', 'App/can', 'App/uart', 'App/adc']
legacy = (ROOT / 'doc/P6a/tests/health_control_host.c').read_text(encoding='utf8')
legacy = legacy.replace('int main(void)', 'int LegacyP6Main(void)')
legacy = legacy.replace('../../P5b/tests/control_display_host.c', (ROOT / 'doc/P5b/tests/control_display_host.c').as_posix())
legacy = legacy.replace('../../../App/health/health_app.c', (ROOT / 'App/health/health_app.c').as_posix())
(OUT / 'legacy_p6.c').write_text(legacy, encoding='utf8')
compile_run('control_metrics', paths('doc/P7/tests/control_metrics_host.c'), [str(OUT), *inc])

# Reuse P6b's UART adapter approach without rewriting historical evidence.
uart = (ROOT / 'doc/P6a/tests/uart_host.c').read_text(encoding='utf8')
uart = uart.replace('../../../App/uart/uart_app.c', (ROOT / 'App/uart/uart_app.c').as_posix())
uart = uart.replace('static HealthBootSnapshot boot;', 'static HealthBootSnapshot boot;\nstatic HealthRuntimeSnapshot runtime;')
uart = uart.replace('void Health_GetBootSnapshot', 'void Health_GetRuntimeSnapshot(HealthRuntimeSnapshot *out) { *out = runtime; }\nvoid Health_GetBootSnapshot')
uart = uart.replace('"WDG=OFF\\r\\n"', '" W=OFF "')
uart = uart.replace('int main(void)', 'int LegacyUartMain(void)')
uart += (ROOT / 'doc/P7/tests/uart_extra.c').read_text(encoding='utf8')
(OUT / 'uart.c').write_text(uart, encoding='utf8')
compile_run('uart', [OUT / 'uart.c'], inc)
compile_run('can_events', paths('doc/P7/tests/can_events_host.c'), ['doc/P7/tests', *inc])

# Existing HealthTask simulator tests production and test-macro guards only
# in native processes (not T2 firmware, not a physical IWDG injection).
compile_run('runtime', paths('doc/P6b/tests/runtime_host.c'), inc)
compile_run('runtime_test_sim', paths('doc/P6b/tests/runtime_host.c'), inc,
            ['/DP6_WDG_TEST_ENABLE=1', '/DP6_WDG_TEST_UNLOADED_ACK=1'])
for defines, message in [
    (['/DP3B_OUTPUT_STAGE=2'], 'Stage2 requires separate explicit user approval'),
    (['/DP3B_OUTPUT_STAGE=3'], 'P3B_OUTPUT_STAGE must be 0..2'),
    (['/DP2B_OUTPUT_STAGE=1'], 'requires explicit compile permission'),
    (['/DP6_WDG_TEST_ENABLE=1'], 'P6 T2 requires Stage0'),
    (['/DP6_WDG_TEST_UNLOADED_ACK=1'], 'Production image must not retain P6 T2')]:
    guard_source = ROOT / ('doc/P6b/tests/runtime_host.c' if 'P6_' in defines[0] else 'App/control/control_policy.c')
    run(['cl.exe', '/nologo', '/TC', '/std:c11', '/c', *defines,
         *['/I' + str(ROOT / p) for p in inc], str(guard_source)], expected=False, message=message)
print(f'PASS: {len(records)} native compile/run/negative checks. Hardware NOT_RUN. Results: {OUT / "results.json"}')
