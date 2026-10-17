"""Build the existing P1a sources with ARMCC5, without editing a Keil project.

python doc/P1/P1a_autotest_check.py --out <new validation directory>
Outputs are validation artifacts, not a second project or release firmware.
Hardware tests are NOT_RUN. Requires a licensed local ARM Compiler 5.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--bin", default=r"D:\MKD5.36\ARM\ARMCC\bin")
parser.add_argument("--out", type=Path)
args = parser.parse_args()
out = args.out or Path(tempfile.mkdtemp(prefix="RTNode_P1a_"))
out.mkdir(parents=True, exist_ok=True)
toolbin = Path(args.bin)
log = []


def run(command, accepted=True):
    log.append(subprocess.list2cmdline([str(x) for x in command]))
    result = subprocess.run([str(x) for x in command], cwd=ROOT,
                            capture_output=True, timeout=120)
    output = (result.stdout + result.stderr).decode("utf-8", errors="replace")
    log.append(output)
    (out / "build.log").write_text("\n".join(log), encoding="utf-8")
    if (result.returncode == 0) != accepted:
        raise RuntimeError(f"Unexpected compiler status {result.returncode}: {output}")
    return output


if not (toolbin / "armcc.exe").is_file():
    print("NOT_RUN: ARMCC5 unavailable at " + str(toolbin))
    raise SystemExit(2)
run([toolbin / "armcc.exe", "--vsn"])
includes = ["App/actuator/include", "Core/Inc", "Driver/CMSIS/Include",
            "Driver/CMSIS/Device/ST/STM32F1xx/Include", "Driver/STM32F1xx_HAL_Driver/Inc"]
sources = ["Core/Src/main.c", "Core/Src/interrupts.c", "App/actuator/src/actuator.c",
           "App/p1a_test/p1a_test.c",
           "Driver/CMSIS/Device/ST/STM32F1xx/Source/Templates/system_stm32f1xx.c"]
sources += ["Driver/STM32F1xx_HAL_Driver/Src/" + name + ".c" for name in
            ("stm32f1xx_hal", "stm32f1xx_hal_rcc", "stm32f1xx_hal_cortex", "stm32f1xx_hal_gpio")]
common = ["--cpu", "Cortex-M3", "--thumb", "--split_sections", "-DSTM32F103xE", "-DUSE_HAL_DRIVER"]
for inc in includes:
    common += ["-I", str(ROOT / inc)]
matrix = [("default", 0, 0, 1), ("mailbox", 1, 0, 1)]
matrix += [(f"auto{case}", 0, 1, case) for case in (1, 2, 3)]
for optimization in (0, 2):
    for name, mailbox, auto, case in matrix:
        dest = out / f"{name}_O{optimization}"
        dest.mkdir(exist_ok=True)
        defines = [f"-DP1A_ACTUATOR_TEST_ENABLE={mailbox}", f"-DP1A_AUTOTEST_ENABLE={auto}",
                   f"-DP1A_AUTOTEST_CASE={case}"]
        if name == "default":
            defines = []  # Exercise actual header defaults, no command-line overrides.
        objects = []
        for source in sources:
            obj = dest / (Path(source).stem + ".o")
            run([toolbin / "armcc.exe", *common, f"-O{optimization}", *defines,
                 "-c", ROOT / source, "-o", obj])
            objects.append(obj)
        startup = dest / "startup.o"
        run([toolbin / "armasm.exe", "--cpu", "Cortex-M3", "--thumb",
             ROOT / "Startup/startup_stm32f103xe.s", "-o", startup])
        objects.append(startup)
        image = dest / "RTnode.axf"
        run([toolbin / "armlink.exe", "--cpu", "Cortex-M3", "--ro_base", "0x08000000",
             "--rw_base", "0x20000000", "--entry", "Reset_Handler", "--first", "__Vectors",
             "--map", "--symbols", "--info", "sizes", "--list", dest / "RTnode.map",
             *objects, "-o", image])
        run([toolbin / "fromelf.exe", "--i32combined", "--output", dest / "RTnode.hex", image])
        symbols = (dest / "RTnode.map").read_text(errors="replace")
        if name == "default":
            assert "P1aAutotest_Run" not in symbols and "g_p1a_test" not in symbols
            assert "Actuator_Enable" not in symbols and "Actuator_Apply" not in symbols
        elif name == "mailbox":
            assert "g_p1a_test" in symbols and "P1aAutotest_Run" not in symbols
        else:
            assert "P1aAutotest_Run" in symbols and "g_p1a_test" not in symbols
        print(f"PASS: ARMCC5 build/link and symbol isolation {dest.name}")
        print("SHA256 " + str(image) + " " + hashlib.sha256(image.read_bytes()).hexdigest())

for name, defines in [
    ("both_modes", ["P1A_ACTUATOR_TEST_ENABLE=1", "P1A_AUTOTEST_ENABLE=1"]),
    ("bad_auto", ["P1A_AUTOTEST_ENABLE=2"]),
    ("bad_mailbox", ["P1A_ACTUATOR_TEST_ENABLE=2"]),
    ("bad_case_zero", ["P1A_AUTOTEST_CASE=0"]),
    ("bad_case_four", ["P1A_AUTOTEST_CASE=4"]),
]:
    errors = run([toolbin / "armcc.exe", *common, *("-D" + x for x in defines),
                  "-c", ROOT / "App/p1a_test/p1a_test.c", "-o", out / (name + ".o")], False)
    assert "#error" in errors or "#35:" in errors
    print("PASS: expected compile rejection " + name)
print("H01-H10: NOT_RUN (hardware). Keil IDE project rebuild: NOT_RUN (configuration preserved).")
print("Build log and MAP/AXF/HEX: " + str(out.resolve()))
