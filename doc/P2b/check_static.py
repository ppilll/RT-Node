"""Small audit of protected code, build identity, and archived real results."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'doc/P2b/evidence'
out.mkdir(parents=True, exist_ok=True)


def git(*args):
    return subprocess.run(['git', '-c', 'core.quotepath=false', *args], cwd=root,
                          capture_output=True, text=True, encoding='utf-8', check=True).stdout.strip()


protected = ['App/actuator', 'App/imu', 'Driver', 'Core', 'Middlewares',
             'App/p1a_test', 'App/p1b_test']
assert not git('diff', 'HEAD', '--name-only', '--', *protected)
assert not git('diff', '--check')
tracked = git('ls-files', '--', *protected).splitlines()
source_files = sorted(set(tracked + [str(p.relative_to(root)).replace('\\', '/')
    for p in (root / 'App/control').glob('*') if p.is_file()] + ['Projects/RTnode.uvprojx']))
manifest = {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in source_files}
allowed = {'App/control/control.c', 'App/control/control.h', 'App/control/control_config.h',
           'App/control/control_policy.c', 'App/control/control_policy.h', 'Projects/RTnode.uvprojx'}
changed = git('diff', 'HEAD', '--name-only').splitlines()
assert set(changed) <= allowed
assert not list(root.rglob('AGENTS.md'))
for parent in [root.parent, root.parent.parent]:
    assert not (parent / 'AGENTS.md').exists()
control = (root / 'App/control/control.c').read_text()
assert 'tskIDLE_PRIORITY + 1U' in control and 'CONTROL_PERIOD_TICKS' in control
assert not re.search(r'TIM3->|GPIOC->|HAL_SPI_|HAL_UART_', control)
assert 'CONTROL_PRIMING_TICKS * CYCLES_PER_TICK' in control
assert 'P2B_OUTPUT_STAGE' not in (root / 'Projects/RTnode.uvprojx').read_text()
stages = {}
for stage in [0, 1]:
    folder = out / f'armcc/stage{stage}'
    log = (folder / 'rebuild.log').read_text(errors='replace')
    m = re.search(r'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)', log)
    assert m and '0 Error(s), 0 Warning(s)' in log
    code, ro, rw, zi = map(int, m.groups())
    text = (folder / 'RTnode.map').read_text()
    if stage == 0:
        assert 'Removing actuator.o(i.Actuator_Enable)' in text
        assert 'Removing actuator.o(i.Actuator_Apply)' in text
        assert 'Removing local_follow.o(i.LocalFollow_Target)' in text
    else:
        assert 'control.o(i.ControlTask) refers to actuator.o(i.Actuator_Enable)' in text
        assert 'control.o(i.ControlTask) refers to actuator.o(i.Actuator_Apply)' in text
    stages[str(stage)] = dict(code=code, ro=ro, rw=rw, zi=zi, flash=code+ro+rw, ram=rw+zi,
        hex_sha256=hashlib.sha256((folder / 'RTnode.hex').read_bytes()).hexdigest(),
        map_sha256=hashlib.sha256((folder / 'RTnode.map').read_bytes()).hexdigest())
records = json.loads((out / 'host/results.json').read_text(encoding='utf-8'))
assert all((r['returncode'] == 0) == r['expected_success'] for r in records)
executed = [r for r in records if r['command'][0] != 'cl.exe']
negative = [r for r in records if not r['expected_success']]
assert len(negative) == 12
result = dict(head=git('rev-parse', 'HEAD'), branch=git('branch', '--show-current'),
    entry_status='?? doc/P2b/ (pre-existing input documents preserved)',
    remote_main_checked='d11fb8b1de2da1a6c18e55970176979b42b36ce7 via git ls-remote HTTPS',
    agents='NONE in repository and applicable ancestors', protected_tracked_unchanged=len(tracked),
    modified_tracked=changed, source_sha256=manifest, stages=stages,
    host_executions=len(executed), negative_compiles=len(negative),
    physical_keys='NOT_VERIFIED', hardware_H01_H04='NOT_RUN',
    real_wcet_and_stack='NOT_MEASURED', stage2_build='NOT_RUN', result='PASS')
(out / 'static.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
(out / 'source.diff').write_text(git('diff', '--', 'App/control', 'Projects/RTnode.uvprojx'), encoding='utf-8')
print(f"STATIC PASS: {len(tracked)} protected tracked files unchanged; {len(executed)} host runs; {len(negative)} guard rejections.")
print(json.dumps(stages, indent=2))
