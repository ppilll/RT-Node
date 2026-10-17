"""Validate C2 ARMCC builds, actual vectors, protected files and guard failures.
Does not execute a board or measure any real-time behavior.
"""
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
evidence = root / "doc/P1b/evidence/C2"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)
    print("PASS:", message)


project = ET.parse(root / "Projects/RTnode.uvprojx").getroot()
targets = project.findall("./Targets/Target")
require(len(targets) == 1, "one Keil target")
target = targets[0]
require(target.findtext("uAC6") == "0" and "5060960" in target.findtext("pCCUsed"), "ARMCC5 target")
defines = target.findtext("./TargetOption/TargetArmAds/Cads/VariousControls/Define")
require("P1B_AUTOTEST_ENABLE=0" in defines and "P1B_TEST_ARM_KEY" not in defines and
        "P1B_UNLOADED_ACK" not in defines, "ordinary project restored unarmed")
sources = [p.text.replace("\\", "/") for p in target.findall("./Groups/Group/Files/File/FilePath")]
require(len(sources) == 18 and len(set(sources)) == 18, "18 unique source entries")
require(sorted(p for p in sources if "/FreeRTOS/" in p) == sorted([
    "../Middlewares/FreeRTOS/tasks.c", "../Middlewares/FreeRTOS/list.c",
    "../Middlewares/FreeRTOS/portable/RVDS/ARM_CM3/port.c"]), "minimal kernel and unique CM3 port")

entry = json.loads((evidence / "entry/hashes.json").read_text(encoding="utf-8-sig"))
protected = [r for r in entry if r["Path"].startswith("App/") or r["Path"] in [
    "Core/Src/interrupts.c", "Core/Src/rtos_hooks.c", "Core/Src/freertos_static.c", "Projects/RTnode.uvoptx"]]
for record in protected:
    require(hashlib.sha256((root / record["Path"]).read_bytes()).hexdigest().upper() == record["SHA256"],
            record["Path"] + " unchanged from entry")

resources = []
for label in ["default", "case1", "case2", "case3", "case4"]:
    folder = evidence / label
    metadata = json.loads((folder / "build.json").read_text(encoding="utf-8-sig"))
    require(metadata["ExitCode"] == 0 and "0 Error(s), 0 Warning(s)" in
            (folder / "rebuild.log").read_text(errors="replace"), label + " real rebuild clean")
    linked_map = (folder / "RTnode.map").read_text(errors="replace")
    binary = (folder / "RTnode.bin").read_bytes()
    for slot, name, owner in [(11, "SVC_Handler", "port.o"),
                               (14, "PendSV_Handler", "port.o"),
                               (15, "SysTick_Handler", "interrupts.o")]:
        matches = re.findall(r"^\s*" + name + r"\s+(0x[0-9a-fA-F]+)\s+Thumb Code\s+\d+\s+(\S+)",
                             linked_map, re.MULTILINE)
        require(len(matches) == 1 and matches[0][1].startswith(owner + "(") and
                struct.unpack_from("<I", binary, slot * 4)[0] == int(matches[0][0], 16),
                label + " actual vector: " + name)
    for name, size, owner in [("s_control_tcb", 72, "control.o"),
                               ("s_control_stack", 1024, "control.o"),
                               ("s_idle_tcb", 72, "freertos_static.o"),
                               ("s_idle_stack", 512, "freertos_static.o")]:
        require(re.search(r"^\s*" + name + r"\s+0x200[0-9a-fA-F]+\s+Data\s+" + str(size) +
                          r"\s+" + re.escape(owner) + r"\(", linked_map, re.MULTILINE),
                label + " static allocation: " + name)
    def has_code(name):
        return bool(re.search(r"^\s*" + name + r"\s+0x[0-9a-fA-F]+\s+Thumb Code", linked_map, re.MULTILINE))
    require(not any(has_code(name) for name in ["pvPortMalloc", "vPortFree", "malloc", "calloc", "realloc", "free"]),
            label + " no dynamic allocation linked")
    for name in ["Actuator_Enable", "Actuator_Apply", "ControlSession_Authorize", "P1bTest_MakeCommand"]:
        require(has_code(name) == (label != "default"), label + " expected test/output code: " + name)
    resource = dict(image=label, defines=metadata["Defines"])
    for key, pattern in [("flash_rom_bytes", r"Total ROM Size.*?\s+(\d+) \("),
                         ("ram_rw_zi_bytes", r"Total RW\s+Size.*?\s+(\d+) \(")]:
        resource[key] = int(re.search(pattern, linked_map).group(1))
    resource["hex_sha256"] = hashlib.sha256((folder / "RTnode.hex").read_bytes()).hexdigest().upper()
    resources.append(resource)
    require(len(binary) == resource["flash_rom_bytes"], label + " binary extent equals ROM total")
require((root / "Projects/RTnode.hex").read_bytes() == (evidence / "default/RTnode.hex").read_bytes(),
        "ordinary Projects/RTnode.hex is the default image")
(evidence / "resources.json").write_text(json.dumps(resources, indent=2), encoding="utf-8")

armcc = r"D:\MKD5.36\ARM\ARMCC\Bin\armcc.exe"
base = [armcc, "-c", "--cpu", "Cortex-M3", "--thumb", "--c99", "-O1", "-DSTM32F103xE", "-DUSE_HAL_DRIVER"]
base += ["-I" + p for p in ["App/actuator/include", "Core/Inc", "Driver/CMSIS/Include",
    "Driver/CMSIS/Device/ST/STM32F1xx/Include", "Driver/STM32F1xx_HAL_Driver/Inc",
    "Middlewares/FreeRTOS/include", "Middlewares/FreeRTOS/portable/RVDS/ARM_CM3"]]
authorized = ["P1B_AUTOTEST_ENABLE=1", "P1B_UNLOADED_ACK=1", "P1B_TEST_ARM_KEY=0xC2201500UL"]
cases = [
    ("enable_only", ["P1B_AUTOTEST_ENABLE=1"], "requires unloaded acknowledgement"),
    ("ack_without_key", ["P1B_AUTOTEST_ENABLE=1", "P1B_UNLOADED_ACK=1"], "requires unloaded acknowledgement"),
    ("bad_key", ["P1B_AUTOTEST_ENABLE=1", "P1B_UNLOADED_ACK=1", "P1B_TEST_ARM_KEY=1"], "requires unloaded acknowledgement"),
    ("invalid_enable", ["P1B_AUTOTEST_ENABLE=2"], "must be 0 or 1"),
    ("invalid_case", authorized + ["P1B_AUTOTEST_CASE=5"], "must be 1..4"),
    ("unarmed_stale_key", ["P1B_TEST_ARM_KEY=0xC2201500UL"], "must not retain P1b test authorization"),
    ("unarmed_stale_case", ["P1B_AUTOTEST_CASE=2"], "must not retain P1b test authorization"),
    ("unapproved_trace", ["P1B_TRACE_ENABLE=1"], "No trace GPIO or diagnostic channel is approved"),
    ("p1a_auto", ["P1A_AUTOTEST_ENABLE=1"], "requires both P1a test modes OFF"),
    ("p1a_mailbox", ["P1A_ACTUATOR_TEST_ENABLE=1"], "requires both P1a test modes OFF"),
    ("mixed_p1a_p1b", authorized + ["P1A_AUTOTEST_ENABLE=1"], "requires both P1a test modes OFF")]
records = []
for label, flags, error in cases:
    command = base + ["-D" + f for f in flags] + ["-o", str(evidence / (label + ".o")), "App/control/control.c"]
    result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace")
    records.append(dict(case=label, command=command, returncode=result.returncode,
                        stdout=result.stdout, stderr=result.stderr))
    (evidence / "compile_guard_results.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
    require(result.returncode != 0 and error in result.stderr, "ARMCC rejects " + label)
print("Board execution, real timing and real stack residual: NOT_RUN (no trace approved).")
