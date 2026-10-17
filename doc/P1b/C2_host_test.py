"""Compile and execute real C2 policy/controller C against explicit host stubs.

MSVC host decisions/call ordering only: NOT FreeRTOS/MCU/LA/stack evidence.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
evidence = root / "doc/P1b/evidence/C2/host"
evidence.mkdir(parents=True, exist_ok=True)
setup = subprocess.run(
    ["cmd.exe", "/d", "/c",
     r"D:\VS_IDE\VC\Auxiliary\Build\vcvars64.bat >nul && set"],
    capture_output=True, text=True, errors="replace", check=True)
env = {key.upper(): value for key, value in os.environ.items()}
for line in setup.stdout.splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        # Environment keys are case insensitive on Windows.
        env[key.upper()] = value

records = []


def run(command, cwd):
    executable = shutil.which(command[0], path=env["PATH"])
    result = subprocess.run(command, executable=executable, cwd=cwd, env=env, capture_output=True,
                            text=True, errors="replace")
    records.append(dict(command=[str(a) for a in command], cwd=str(cwd),
                        returncode=result.returncode, stdout=result.stdout, stderr=result.stderr))
    (evidence / "results.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    if command[0] != "cl.exe":
        print(result.stdout.strip())


for case in range(5):
    folder = evidence / str(case)
    folder.mkdir(exist_ok=True)
    flags = ["cl.exe", "/nologo", "/TC", "/std:c11", "/W4", "/WX", "/O2",
             "/I" + str(root / "doc/P1b/tests/stubs"),
             "/I" + str(root / "App/control"),
             "/I" + str(root / "App/actuator/include")]
    if case:
        flags += ["/DP1B_AUTOTEST_ENABLE=1", "/DP1B_UNLOADED_ACK=1",
                  "/DP1B_TEST_ARM_KEY=0xC2201500UL", "/DP1B_AUTOTEST_CASE=" + str(case)]
    run(flags + [str(root / p) for p in ["App/control/control.c",
                "App/control/control_policy.c", "App/p1b_test/p1b_test.c",
                "doc/P1b/tests/controller_host_test.c"]] + ["/Fecontroller.exe"], folder)
    scenarios = ["nominal"]
    if case in (0, 1):
        scenarios += ["rollover", "no_counter", "create_fail"]
    if case == 1:
        scenarios += ["no_wait", "external_fault", "refuse_enable", "refuse_apply"]
    for scenario in scenarios:
        run([str(folder / "controller.exe"), scenario], folder)

run(["cl.exe", "/nologo", "/TC", "/std:c11", "/W4", "/WX", "/O2",
     "/I" + str(root / "App/control"), str(root / "App/control/control_policy.c"),
     str(root / "doc/P1b/tests/policy_test.c"), "/Fepolicy.exe"], evidence)
run([str(evidence / "policy.exe")], evidence)
print("PASS: host C execution only. Hardware timing, PWM and stack residual NOT_RUN.")
