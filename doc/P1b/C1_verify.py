"""C1 build-artifact/compile-guard checks; does not execute MCU firmware.

Run after rebuilding the sole Keil target and exporting RTnode.bin with fromelf.
No board, emulator, P1a waveform test, or C2 timing test is run here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import xml.etree.ElementTree as ET


def require(condition, message):
    if not condition:
        raise RuntimeError(message)
    print("PASS:", message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--armcc", default=r"D:\MKD5.36\ARM\ARMCC\Bin\armcc.exe")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    evidence = root / "doc/P1b/evidence/C1"
    project = ET.parse(root / "Projects/RTnode.uvprojx").getroot()
    targets = project.findall("./Targets/Target")
    require(len(targets) == 1, "one Keil target")
    target = targets[0]
    require(target.findtext("uAC6") == "0" and
            "5060960" in target.findtext("pCCUsed"), "ARMCC5 selected")
    sources = [n.text.replace("\\", "/") for n in target.findall("./Groups/Group/Files/File/FilePath")]
    kernel = [p for p in sources if "/FreeRTOS/" in p]
    require(sorted(kernel) == sorted([
        "../Middlewares/FreeRTOS/tasks.c", "../Middlewares/FreeRTOS/list.c",
        "../Middlewares/FreeRTOS/portable/RVDS/ARM_CM3/port.c"]),
        "only tasks/list/RVDS ARM_CM3; no FreeRTOS heap or unused middleware")

    linked_map = (evidence / "RTnode.map").read_text(errors="replace")
    binary = (evidence / "RTnode.bin").read_bytes()
    for slot, name, owner in [(11, "SVC_Handler", "port.o"),
                               (14, "PendSV_Handler", "port.o"),
                               (15, "SysTick_Handler", "interrupts.o")]:
        matches = re.findall(r"^\s*" + name +
                             r"\s+(0x[0-9a-fA-F]+)\s+Thumb Code\s+\d+\s+(\S+)",
                             linked_map, re.MULTILINE)
        require(len(matches) == 1 and matches[0][1].startswith(owner + "("),
                name + " has one linked definition from " + owner)
        require(struct.unpack_from("<I", binary, slot * 4)[0] == int(matches[0][0], 16),
                name + " binary vector points to that definition")

    for name, size, owner in [("s_control_tcb", 72, "main.o"),
                               ("s_control_stack", 1024, "main.o"),
                               ("s_idle_tcb", 72, "freertos_static.o"),
                               ("s_idle_stack", 512, "freertos_static.o")]:
        require(re.search(r"^\s*" + name + r"\s+0x200[0-9a-fA-F]+\s+Data\s+" +
                          str(size) + r"\s+" + re.escape(owner) + r"\(",
                          linked_map, re.MULTILINE), name + " static SRAM allocation/size")
    for name in ["Actuator_Enable", "Actuator_Apply", "Actuator_Disable",
                 "pvPortMalloc", "vPortFree", "malloc", "calloc", "realloc", "free"]:
        require(not re.search(r"^\s*" + name + r"\s+0x[0-9a-fA-F]+\s+Thumb Code",
                              linked_map, re.MULTILINE), name + " absent from linked code")
    for name in ["xTaskCreateStatic", "vTaskStartScheduler", "AppFatal_Assert",
                 "vApplicationStackOverflowHook", "vApplicationGetIdleTaskMemory"]:
        require(re.search(r"^\s*" + name + r"\s+0x[0-9a-fA-F]+\s+Thumb Code",
                          linked_map, re.MULTILINE), name + " linked")

    entry = json.loads((evidence / "entry/source_hashes.json").read_text(encoding="utf-8-sig"))
    for record in entry:
        path = Path(record["Path"])
        if path.parent.name in ("p1a_test", "src", "include") and "App" in path.parts:
            require(hashlib.sha256(path.read_bytes()).hexdigest().upper() == record["Hash"],
                    str(path.relative_to(root)) + " unchanged since entry")

    include_dirs = ["App/actuator/include", "Core/Inc", "Driver/CMSIS/Include",
                    "Driver/CMSIS/Device/ST/STM32F1xx/Include",
                    "Driver/STM32F1xx_HAL_Driver/Inc", "Middlewares/FreeRTOS/include",
                    "Middlewares/FreeRTOS/portable/RVDS/ARM_CM3"]
    base = [args.armcc, "-c", "--cpu", "Cortex-M3", "--thumb", "--c99", "-O1",
            "-DSTM32F103xE", "-DUSE_HAL_DRIVER"] + ["-I" + p for p in include_dirs]
    cases = [("default", [], None),
             ("reject_p1a_autotest", ["-DP1A_AUTOTEST_ENABLE=1"], "C1 requires both P1a test modes OFF"),
             ("reject_p1a_mailbox", ["-DP1A_ACTUATOR_TEST_ENABLE=1"], "C1 requires both P1a test modes OFF"),
             ("reject_p1b_test", ["-DP1B_AUTOTEST_ENABLE=1"], "C1 has no P1b output test")]
    records = []
    for label, defines, expected_error in cases:
        command = base + defines + ["-o", str(evidence / (label + ".o")), "Core/Src/main.c"]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace")
        records.append(dict(case=label, command=command, returncode=result.returncode,
                            stdout=result.stdout, stderr=result.stderr))
        (evidence / "compile_guard_results.json").write_text(
            json.dumps(records, indent=2, ensure_ascii=False), encoding="utf-8")
        require((result.returncode == 0) if expected_error is None else
                (result.returncode != 0 and expected_error in result.stderr), label)
    print("Board execution, HAL/RTOS tick accuracy, timing, stack residual: NOT_RUN")


if __name__ == "__main__":
    main()
