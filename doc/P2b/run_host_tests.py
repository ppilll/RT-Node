"""Compile actual production C with MSVC; virtual HAL/RTOS is not hardware evidence."""
import json
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'doc/P2b/evidence/host'
out.mkdir(parents=True, exist_ok=True)
setup_path = os.environ.get('P2B_VCVARS', r'D:\VS_IDE\VC\Auxiliary\Build\vcvars64.bat')
setup = subprocess.run(['cmd.exe', '/d', '/c', 'call ' + subprocess.list2cmdline([setup_path]) + ' >nul && set'],
                       capture_output=True, text=True, errors='replace', check=True)
env = {k.upper(): v for k, v in os.environ.items()}
for line in setup.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key.upper()] = value
records = []


def run(args, folder=out, success=True, message=None):
    folder.mkdir(parents=True, exist_ok=True)
    p = subprocess.run(args, executable=shutil.which(args[0], path=env['PATH']),
                       cwd=folder, env=env, capture_output=True, text=True, errors='replace')
    records.append(dict(command=args, cwd=str(folder), returncode=p.returncode,
                        expected_success=success, stdout=p.stdout, stderr=p.stderr))
    (out / 'results.json').write_text(json.dumps(records, indent=2, ensure_ascii=False), encoding='utf-8')
    if (p.returncode == 0) != success or (message and message not in p.stdout + p.stderr):
        raise RuntimeError(p.stdout + p.stderr)
    if args[0] != 'cl.exe':
        print(p.stdout.strip())


flags = ['cl.exe', '/nologo', '/TC', '/std:c11', '/W4', '/WX', '/O2']
flags += ['/I' + str(root / p) for p in ['doc/P2b/tests/stubs', 'doc/P1b/tests/stubs',
          'App/control', 'App/actuator/include', 'App/imu/include']]
policy = str(root / 'App/control/control_policy.c')
mapping = str(root / 'App/control/local_follow.c')
for sign in [1, -1]:
    folder = out / ('mapping' + str(sign))
    run(flags + ['/DP2B_PAN_SIGN=' + str(sign), '/DP2B_TILT_SIGN=' + str(sign),
                 policy, mapping, str(root / 'doc/P2b/tests/local_test.c'), '/Felocal.exe'], folder)
    run([str(folder / 'local.exe')], folder)
run(flags + [policy, str(root / 'doc/P1b/tests/policy_test.c'), '/Fepolicy.exe'])
run([str(out / 'policy.exe')])

stage1 = ['/DP2B_OUTPUT_STAGE=1', '/DP2B_UNLOADED_ACK=1', '/DP2B_OUTPUT_ARM_KEY=0xB2201500UL']
for stage in [0, 1]:
    folder = out / ('stage' + str(stage))
    run(flags + (stage1 if stage else []) + [str(root / 'App/control/control.c'), policy, mapping,
        str(root / 'doc/P2b/tests/controller_test.c'), '/Fecontroller.exe'], folder)
    scenarios = ['nominal', 'no_key', 'boot_hold', 'boot_release_press', 'key0_bounce', 'both_keys']
    if stage:
        scenarios += ['rollover', 'pre_ready_press', 'prime_dwt', 'key1_bounce', 'stop_before_arm', 'failed_before_arm',
                      'fresh39', 'stale', 'future', 'duplicate', 'failed', 'invalid', 'not_ready',
                      'unavailable', 'nan', 'inf', 'prime_nan', 'refuse_enable', 'refuse_apply',
                      'external', 'deadline', 'execution', 'clock']
    for scenario in scenarios:
        run([str(folder / 'controller.exe'), scenario], folder)

# Unmodified historical TEST controller fixture: all four finite P1b cases.
# Isolated Control build only: unchanged P2a imu.c still forbids P1 test images.
for case in range(5):
    folder = out / ('test' + str(case))
    defines = [] if not case else ['/DP1B_AUTOTEST_ENABLE=1', '/DP1B_UNLOADED_ACK=1',
                                  '/DP1B_TEST_ARM_KEY=0xC2201500UL', '/DP1B_AUTOTEST_CASE=' + str(case)]
    run(flags + defines + [str(root / p) for p in ['App/control/control.c',
        'App/control/control_policy.c', 'App/p1b_test/p1b_test.c',
        'doc/P1b/tests/controller_host_test.c']] + ['/Fecontroller.exe'], folder)
    scenarios = ['nominal']
    if case in (0, 1):
        scenarios += ['rollover', 'no_counter', 'create_fail']
    if case == 1:
        scenarios += ['no_wait', 'external_fault', 'refuse_enable', 'refuse_apply']
    for scenario in scenarios:
        run([str(folder / 'controller.exe'), scenario], folder)

# Negative compilation must fail on the intended permission guard.
guards = [
    (['/DP2B_OUTPUT_STAGE=3'], 'P2B_OUTPUT_STAGE must be 0..2'),
    (['/DP2B_PAN_SIGN=0'], 'P2b axis signs must be +1 or -1'),
    (['/DP2B_OUTPUT_ARM_KEY=1'], 'Stage0 must not retain'),
    (['/DP2B_OUTPUT_STAGE=1'], 'requires explicit compile permission'),
    (['/DP2B_OUTPUT_STAGE=1', '/DP2B_OUTPUT_ARM_KEY=0xB2201500UL'], 'Stage1 requires'),
    (stage1 + ['/DP2B_OPERATOR_ACK=1'], 'Stage1 requires'),
    (stage1 + ['/DP1B_AUTOTEST_ENABLE=1', '/DP1B_UNLOADED_ACK=1',
               '/DP1B_TEST_ARM_KEY=0xC2201500UL'], 'mutually exclusive'),
    (stage1 + ['/DP1A_AUTOTEST_ENABLE=1'], 'requires both P1a test modes OFF'),
    (stage1 + ['/DP1A_ACTUATOR_TEST_ENABLE=1'], 'requires both P1a test modes OFF'),
    (['/DP2B_OUTPUT_STAGE=2', '/DP2B_OUTPUT_ARM_KEY=0xB2201500UL'], 'Stage2 requires'),
    (['/DP2B_OUTPUT_STAGE=2', '/DP2B_OUTPUT_ARM_KEY=0xB2201500UL',
      '/DP2B_G2_EVIDENCE_ACK=1'], 'Stage2 requires'),
    (['/DP2B_OUTPUT_STAGE=2', '/DP2B_OUTPUT_ARM_KEY=0xB2201500UL',
      '/DP2B_OPERATOR_ACK=1'], 'Stage2 requires'),
]
for defines, message in guards:
    run(flags + ['/c'] + defines + [policy], out / 'guards', success=False, message=message)
print('HOST PASS U01-U03 and historical TEST regression; hardware/real WCET NOT_RUN.')
