"""Exercise actual P3b C decisions; synthetic tests never imply hardware PASS."""
import json
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'doc/P3b/evidence/host'
out.mkdir(parents=True, exist_ok=True)
setup = subprocess.run(
    ['cmd.exe', '/d', '/c', 'call ' + subprocess.list2cmdline([
        os.environ.get('P3B_VCVARS', r'D:\VS_IDE\VC\Auxiliary\Build\vcvars64.bat')]) + ' >nul && set'],
    capture_output=True, text=True, errors='replace', check=True)
env = {k.upper(): v for k, v in os.environ.items()}
for line in setup.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        k, v = line.split('=', 1)
        env[k.upper()] = v
records = []


def run(args, folder, expected=0, message=None):
    folder.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(args, executable=shutil.which(args[0], path=env['PATH']),
                            cwd=folder, env=env, capture_output=True, text=True, errors='replace')
    records.append(dict(command=args, returncode=result.returncode,
                        stdout=result.stdout, stderr=result.stderr, expected=expected))
    (out / 'results.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
    if (result.returncode == 0) != (expected == 0) or (message and message not in result.stdout + result.stderr):
        raise RuntimeError(result.stdout + result.stderr)
    if args[0] != 'cl.exe':
        print(result.stdout.strip())


flags = ['cl.exe', '/nologo', '/TC', '/std:c11', '/W4', '/WX', '/O2']
flags += ['/I' + str(root / p) for p in [
    'doc/P2b/tests/stubs', 'doc/P1b/tests/stubs', 'App/control',
    'App/actuator/include', 'App/imu/include']]
sources = [str(root / p) for p in ['App/control/control.c', 'App/control/control_policy.c',
           'App/control/local_follow.c', 'doc/P3b/tests/controller_test.c']]
for stage in [0, 1]:
    folder = out / f'stage{stage}'
    run(flags + [f'/DP3B_OUTPUT_STAGE={stage}'] + sources + ['/Fecontroller.exe'], folder)
    for case in ['nominal', 'busy', 'duplicate', 'mode_errors', 'stop', 'timeout', 'overflow',
                 'invalid', 'remote_imu_failed', 'rollover', 'switch', 'local', 'boot_hold',
                 'fault', 'local_fault', 'limits']:
        run([str(folder / 'controller.exe'), case], folder)
run(flags + [sources[1], str(root / 'doc/P1b/tests/policy_test.c'), '/Fepolicy.exe'], out / 'policy')
run([str(out / 'policy/policy.exe')], out / 'policy')
for defines, message in [
    (['/DP3B_OUTPUT_STAGE=2'], 'Stage2 requires separate explicit user approval'),
    (['/DP3B_OUTPUT_STAGE=3'], 'P3B_OUTPUT_STAGE must be 0..2'),
    (['/DP3B_LOADED_APPROVED=1'], 'Loaded approval belongs only to Stage2'),
    (['/DP2B_OUTPUT_STAGE=1', '/DP2B_UNLOADED_ACK=1', '/DP2B_OUTPUT_ARM_KEY=0xB2201500UL'],
     'P3b replaces P2b output test stages')]:
    run(flags + defines + ['/c', sources[1]], out / 'guards', expected=1, message=message)
print('P3b host checks PASS; hardware/WCET NOT_TESTED.')
