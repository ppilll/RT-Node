"""Verify a Stage0 Rebuild's effective preprocessor macros, MAP and HEX.

No download/debug/target connection. Called by ../rebuild.ps1.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'doc/P6a'))
from check_hex import check


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--armcc', type=Path, required=True)
    args = p.parse_args()
    folder = args.directory.resolve()
    project = ET.parse(folder / 'build_project.uvprojx')
    control = project.find('.//Cads/VariousControls')
    defines = control.findtext('Define').split(',')
    includes = control.findtext('IncludePath').split(';')
    command = [str(args.armcc), '--cpu', 'Cortex-M3', '--c99', '-E', '--list_macros']
    command += ['-D' + x for x in defines if x]
    command += ['-I' + str((folder / x).resolve()) for x in includes if x]
    # Health config includes Control config, all production safety macros.
    probe = folder / 'macro_probe.c'
    include = os.path.relpath(ROOT / 'App/health/health_config.h', folder).replace('\\', '/')
    probe.write_text('#include "' + include + '"\n', encoding='ascii')
    result = subprocess.run(command + [str(probe)], cwd=folder, capture_output=True, check=True)
    macros_text = result.stdout.decode('utf8', errors='replace')
    (folder / 'effective_macros.txt').write_text(macros_text, encoding='utf8')
    expected = dict(P3B_OUTPUT_STAGE='0', P2B_OUTPUT_STAGE='0', P6_WDG_TEST_ENABLE='0',
                    P6_WDG_TEST_UNLOADED_ACK='0', P1A_AUTOTEST_ENABLE='0', P1A_ACTUATOR_TEST_ENABLE='0',
                    P1B_AUTOTEST_ENABLE='0', CONTROL_PERIOD_TICKS='20U', CONTROL_EXECUTION_US='2000U',
                    HEALTH_STALL_TICKS='300U')
    for key, value in expected.items():
        assert re.search(r'^#define\s+' + key + r'\s+' + re.escape(value) + r'\s*$', macros_text, re.M), key
    actual = subprocess.run(command + [str(ROOT / 'App/control/control.c')], cwd=folder, capture_output=True, check=True)
    preprocessed = actual.stdout.decode('utf8', errors='replace')
    (folder / 'control_preprocessed.c').write_text(preprocessed, encoding='utf8')
    assert not re.search(r'static\s+void\s+Motion\s*\(', preprocessed), 'Stage0 must not compile Motion'
    assert not re.search(r'static\s+void\s+Apply\s*\(', preprocessed), 'Stage0 must not compile Apply wrapper'
    maptext = (folder / 'Listings/P7_STAGE0.map').read_text(encoding='utf8', errors='replace')
    totals = re.search(r'^\s*(\d+)\s+\d+\s+(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+Grand Totals', maptext, re.M)
    assert totals, 'MAP Grand Totals missing'
    code, ro, rw, zi = map(int, totals.groups())
    hexfile = folder / 'P7_STAGE0.hex'
    summary = dict(status='PASS', code=code, ro=ro, rw=rw, zi=zi, flash=code + ro + rw,
                   static_ram=rw + zi, effective_macros=expected, motion_compiled=False,
                   hex_check=check(hexfile), hex_sha256=hashlib.sha256(hexfile.read_bytes()).hexdigest(),
                   hardware='NOT_RUN', download='NOT_RUN')
    (folder / 'validation.json').write_text(json.dumps(summary, indent=2), encoding='utf8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
