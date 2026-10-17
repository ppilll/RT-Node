"""Read-only P2a safety/integration audit against the actual Git HEAD."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]


def git(*args):
    return subprocess.check_output(['git', *args], cwd=root)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def normalized(data):
    return data.replace(b'\r\n', b'\n')


protected_roots = ['App/actuator', 'App/control', 'Middlewares', 'Startup',
    'Core/Inc/FreeRTOSConfig.h', 'Core/Inc/stm32f1xx_hal_conf.h', 'Core/Src/interrupts.c',
    'Core/Src/rtos_hooks.c', 'Core/Src/freertos_static.c', 'Driver/STM32F1xx_HAL_Driver',
    'Driver/CMSIS']
protected = git('ls-files', *protected_roots).decode('utf-8').splitlines()
assert not git('diff', '--name-only', 'HEAD', '--', *protected_roots).strip()
original_main = normalized(git('show', 'HEAD:Core/Src/main.c')).decode('utf-8')
expected_main = original_main.replace('#include "../../App/control/control_config.h"',
    '#include "../../App/control/control_config.h"\n#include "imu.h"')
needle = '    if (!Control_Init())\n    {\n        P1a_FatalError();\n    }\n'
expected_main = expected_main.replace(needle, needle +
    '    if (!Imu_Init())\n    {\n        P1a_FatalError();\n    }\n')
assert (root/'Core/Src/main.c').read_text(encoding='utf-8') == expected_main
project = ET.parse(root/'Projects/RTnode.uvprojx').getroot()
old_project = ET.fromstring(git('show', 'HEAD:Projects/RTnode.uvprojx'))
old_files = [x.text for x in old_project.findall('.//FilePath')]
files = [x.text for x in project.findall('.//FilePath')]
include_node = './/Cads/VariousControls/IncludePath'
assert project.findtext(include_node) == old_project.findtext(include_node) + ';..\\App\\imu\\include;..\\Driver\\imu'
assert [p for p in files if p in old_files] == old_files
assert len(files) == len(set(files)) == len(old_files)+6
for p in ['stm32f1xx_hal_spi.c', 'stm32f1xx_hal_dma.c', 'icm20602.c',
          'icm20602_data.c', 'imu_fusion.c', 'imu.c']:
    assert sum(x.endswith('\\'+p) for x in files) == 1
# Nothing outside include path and appended File/Group nodes may change.
for tree in (project, old_project):
    for node in tree.findall('.//Cads/VariousControls/IncludePath'):
        node.text = (old_project.find('.//Cads/VariousControls/IncludePath').text)
    groups = tree.find('.//Groups')
    for group in list(groups):
        if group.findtext('GroupName') == 'IMU':
            groups.remove(group)
        else:
            entries = group.find('Files')
            for entry in list(entries):
                if entry.findtext('FilePath') not in old_files:
                    entries.remove(entry)
assert ET.tostring(project).split() == ET.tostring(old_project).split()
source_paths = [root/'Projects'/p.replace('\\','/') for p in files]
source_paths += list((root/'App/imu/include').glob('*.h')) + list((root/'Driver/imu').glob('*.h'))
source_paths += [root/'Projects/RTnode.uvprojx']
source_hashes = {p.resolve().relative_to(root).as_posix(): sha(p.read_bytes()) for p in source_paths}
for folder in ['App/imu', 'Driver/imu']:
    for p in (root/folder).rglob('*.c'):
        code = p.read_text(encoding='utf-8')
        assert not re.search(r'Actuator_(?:Enable|Apply)|TIM3\s*->|AFIO\s*->|\b(?:malloc|pvPortMalloc|vTaskDelay)\s*\(|HAL_SPI_\w*(?:DMA|_IT)\s*\(', code), p
fusion = (root/'App/imu/src/imu_fusion.c').read_text(encoding='utf-8')
assert not re.search(r'(?i)include.*(?:HAL|FreeRTOS|CMSIS)', fusion)
result = dict(status='PASS', category='STATIC', head=git('rev-parse','HEAD').decode().strip(),
    branch=git('branch','--show-current').decode().strip(), protected_files_unchanged=len(protected),
    main_changes='imu.h include + Imu_Init fatal-path check only',
    project_changes='append IMU group, HAL SPI/DMA sources and two include paths only',
    temp1_log_sha256=sha((root/'temp1.log').read_bytes()), source_sha256=source_hashes)
out = root/'doc/P2a/evidence/static.json'
out.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
print(f'S01 PASS: {len(protected)} protected files unchanged; main/project integration minimal; no IMU output/DMA/IRQ calls.')
print(f'Source manifest: {out.relative_to(root)}')
