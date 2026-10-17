"""MSVC /W4 /WX, actual production C; mock HAL/RTOS is NOT hardware evidence."""
import json
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'doc/P2a/evidence/host'
out.mkdir(parents=True, exist_ok=True)
setup_path = os.environ.get('P2A_VCVARS', r'D:\VS_IDE\VC\Auxiliary\Build\vcvars64.bat')
setup = subprocess.run(['cmd.exe', '/d', '/c', 'call ' + subprocess.list2cmdline([setup_path]) + ' >nul && set'],
                       capture_output=True, text=True, errors='replace', check=True)
env = {k.upper(): v for k, v in os.environ.items()}
for line in setup.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key.upper()] = value
records = []


def run(args, expected_success=True):
    p = subprocess.run(args, executable=shutil.which(args[0], path=env['PATH']),
                       cwd=out, env=env, capture_output=True, text=True, errors='replace')
    records.append(dict(command=args, returncode=p.returncode, stdout=p.stdout, stderr=p.stderr))
    (out / 'results.json').write_text(json.dumps(records, indent=2, ensure_ascii=False), encoding='utf-8')
    print(p.stdout.strip())
    if (p.returncode == 0) != expected_success:
        raise RuntimeError(p.stdout + p.stderr)


flags = ['cl.exe', '/nologo', '/TC', '/std:c11', '/W4', '/WX', '/O2']
flags += ['/I' + str(root / p) for p in ['doc/P2a/tests/stubs', 'App/imu/include', 'Driver/imu']]
sources = ['App/imu/src/imu_fusion.c', 'Driver/imu/icm20602_data.c']
run(flags + [str(root / p) for p in sources + ['doc/P2a/tests/fusion_test.c']] + ['/Fefusion.exe'])
run([str(out / 'fusion.exe')])
all_sources = sources + ['Driver/imu/icm20602.c', 'App/imu/src/imu.c', 'doc/P2a/tests/task_driver_test.c']
run(flags + [str(root / p) for p in all_sources] + ['/Fetask_driver.exe'])
for scenario in ['nominal', 'rollover', 'late_wake', 'create_fail', 'id_mismatch', 'reset_stuck',
                 'config_mismatch', 'init_io', 'partial_frame', 'burst_timeout',
                 'motion_timeout', 'invalid_accel', 'zero_dt', 'long_dt']:
    run([str(out / 'task_driver.exe'), scenario])
# Prove P2a cannot accidentally be combined with an armed P1b test build.
run(flags + ['/c', '/DP1B_AUTOTEST_ENABLE=1', '/DP1B_UNLOADED_ACK=1',
             '/DP1B_TEST_ARM_KEY=0xC2201500UL', str(root / 'App/imu/src/imu.c')], False)
assert 'P2a requires all actuator output test modes OFF' in records[-1]['stdout']
print('HOST PASS: U01-U04, actual C algorithms/driver/task. MCU/LA/MANUAL NOT_RUN.')
