"""C2 source checks + independent register-semantics models. No C compilation.
Run from any cwd: python doc/P1/C2_static_check.py
This is neither a device emulator nor execution of the actuator C code.
"""
from pathlib import Path
from hashlib import sha256
from itertools import product
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE_PATHS = ["Core/Src/main.c", "Core/Inc/p1a_safety.h",
                "Core/Src/interrupts.c", "App/actuator/include/actuator.h",
                "App/actuator/src/actuator.c", "App/p1a_test/p1a_test.h",
                "App/p1a_test/p1a_test.c"]
sources = {p: (ROOT / p).read_text(encoding="utf-8") for p in SOURCE_PATHS}
driver = sources["App/actuator/src/actuator.c"]
main = sources["Core/Src/main.c"]
interrupts = sources["Core/Src/interrupts.c"]


def check(condition, description):
    if not condition:
        raise AssertionError(description)
    print("PASS (static/model): " + description)


def body(text, name):
    start = re.search(r"\b" + name + r"\([^;]*?\)\s*\{", text).end()
    depth, end = 1, start
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end - 1]


print("C2 checks; 2026-10-16 Asia/Shanghai")
print("NO compiler/linker/MCU/hardware execution; model assumptions: RM0008 UEV/OC preload.")
check(not re.search(r"GPIOA|IOPAEN|PA6|PA7|FreeRTOS|protocol_transport|gimbal_", driver),
      "actuator has no old pin/kernel/protocol dependency")
check(len(re.findall(r"^(?:bool|void) Actuator_\w+\(", driver, re.M)) == 6,
      "six public actuator APIs")
check(re.search(r"#define P1A_ACTUATOR_TEST_ENABLE 0\b",
                sources["App/p1a_test/p1a_test.h"]), "test default OFF")
test_source = sources["App/p1a_test/p1a_test.c"]
mailbox_source = test_source.split("#if P1A_ACTUATOR_TEST_ENABLE", 1)[1]
check("s_wait_count < 65536U" in mailbox_source and
      "Actuator_ForceSafeOutput();" in mailbox_source and
      "while (" not in mailbox_source and "HAL_GetTick" not in mailbox_source,
      "boundary test is finite nonblocking polling and faults closed without Tick")
check("#define P1A_AUTOTEST_ENABLE 0" in sources["App/p1a_test/p1a_test.h"] and
      "#if P1A_AUTOTEST_ENABLE && P1A_ACTUATOR_TEST_ENABLE" in sources["App/p1a_test/p1a_test.h"],
      "automatic test default OFF; mailbox mutually exclusive")
auto_source = test_source.split("#if P1A_ACTUATOR_TEST_ENABLE", 1)[0]
check(not re.search(r"(?:TIM3|GPIOC|AFIO)->\w+\s*(?:=(?!=)|\|=|&=)", auto_source) and
      "HAL_Delay(" not in auto_source and "__WFI(" not in auto_source and
      "poll < AUTO_POLL_BUDGET" in auto_source,
      "automatic test owns no PWM register writes; finite active waits")
check(body(main, "main").lstrip().startswith("/* First action") and
      body(main, "main").index("P1a_ForceSafeOutput();") < body(main, "main").index("HAL_Init()"),
      "cold-start raw shutdown precedes HAL")
check("Actuator_Enable" not in body(main, "main") and "P1aTest_Poll();" in main,
      "main has no direct Enable; test poll behind compile-time switch (manual review)")
raw = body(main, "P1a_ForceSafeOutput")
check(raw.index("TIM3->CCER") < raw.index("TIM3->CR1") < raw.index("GPIOC->BRR") < raw.index("GPIOC->CRL"),
      "shutdown hardware order: CCER, CEN, preload GPIO low, GPIO mode")
check("HAL_" not in raw and "while" not in raw and "for (" not in raw,
      "raw shutdown has no HAL/wait")
check(body(driver, "Actuator_ForceSafeOutput").index("P1a_ForceSafeOutput();") <
      body(driver, "Actuator_ForceSafeOutput").index("s_fault_latched = 1U"),
      "emergency closes hardware before sticky aligned-word latch")
check(len(re.findall(r"s_fault_latched\s*=(?!=)", driver)) == 1,
      "no software latch-clear assignment")
for name in ("NMI", "HardFault", "MemManage", "BusFault", "UsageFault", "SVC", "PendSV", "DebugMon"):
    check(body(interrupts, name + "_Handler").strip() == "P1a_FatalError();",
          name + " enters non-returning fatal path")
check("for (;;)" in body(main, "P1a_FatalError") and
      "Actuator_ForceSafeOutput();" in body(main, "P1a_FatalError"),
      "fatal path latches and never resumes interrupted normal MMIO")
check("__set_PRIMASK(saved)" in driver and "__enable_irq" not in driver,
      "normal critical sections preserve PRIMASK")
apply = body(driver, "Actuator_Apply")
check("TIM3->EGR" not in apply and apply.index("TIM_CR1_UDIS;") <
      apply.index("TIM3->CCR1 =") < apply.index("TIM3->CCR2 =") <
      apply.index("TIM3->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;"),
      "Apply brackets paired preloads with UDIS; no running UG")
check("s_center_pending && ((TIM3->SR & TIM_SR_UIF) == 0U)" in apply,
      "Apply before first center transfer fails closed")
check("~(AFIO_MAPR_SWJ_CFG | AFIO_MAPR_TIM3_REMAP)" in driver and
      "AFIO_MAPR_SWJ_CFG_RESET | AFIO_MAPR_TIM3_REMAP_FULLREMAP" in driver and
      "__HAL_AFIO_REMAP" not in driver,
      "MAPR masks undefined SWJ readback and explicitly writes full debug + full remap")
# All possible undefined SWJ readback values; several unrelated remap patterns.
swj, tim3 = 0x07000000, 0xC00
for unknown, other in product(range(8), (0, 0x155555, 0x1FFFFF)):
    before = other | (unknown << 24)
    after = (before & ~(swj | tim3)) | tim3
    assert after & swj == 0 and after & tim3 == tim3
    assert (after & ~(swj | tim3)) == (before & ~(swj | tim3))
check(True, "MAPR mask arithmetic: 24 patterns preserve other bits and force SWJ=000")
for crl in (0, 0xFFFFFFFF, 0x12345678):
    for mode in (0x22000000, 0xAA000000):
        after = (crl & ~0xFF000000) | mode
        assert after & 0xFFFFFF == crl & 0xFFFFFF
check(True, "GPIO mask preserves PC0-PC5; safe=0x22, AF=0xAA")
# Enumerate an overflow at every boundary of the 4-write Apply transaction,
# including combinations of overflows. Stronger than the real short window.
for events in product((False, True), repeat=5):
    active, preload, udis = (1500, 1500), [1500, 1500], False
    for index in range(5):
        if events[index] and not udis:
            active = tuple(preload)
        assert active in ((1500, 1500), (2000, 1200))
        if index == 0: udis = True
        if index == 1: preload[0] = 2000
        if index == 2: preload[1] = 1200
        if index == 3: udis = False
    active = tuple(preload)  # following UEV
    assert active == (2000, 1200)
check(True, "UEV model: 32 overflow schedules never transfer a mixed axis pair")
# Opening LOW frame + complete first center pulse under PWM1 comparator rule.
active = (0, 0)
high_counts = [0, 0]
for elapsed in range(40000):
    counter = elapsed % 20000
    if elapsed == 20000: active = (1500, 1500)
    levels = tuple(counter < ccr for ccr in active)
    if elapsed < 20000: assert levels == (False, False)
    for axis in range(2): high_counts[axis] += levels[axis]
check(high_counts == [1500, 1500], "PWM model: LOW opening frame then complete dual center pulse")
# Check used register symbols exist in the actual repository headers (not preprocessing).
headers = "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in
                    (ROOT / "Driver").rglob("*.h"))
tokens = set(re.findall(r"\b(?:TIM_|RCC_|AFIO_|GPIO_)[A-Z0-9_]+\b", driver + raw))
missing = [t for t in sorted(tokens) if not re.search(r"#define\s+" + t + r"\b", headers)]
check(not missing, "all used register constants found in local headers: " + str(missing))
check(subprocess.run(["git", "diff", "--check"], cwd=ROOT, capture_output=True).returncode == 0,
      "git diff --check")
check(all(not line.rstrip("\r\n").endswith((" ", "\t"))
          for value in sources.values() for line in value.splitlines(True)),
      "new and existing source files have no trailing whitespace")
for path in SOURCE_PATHS + ["Projects/RTnode.uvprojx", "Projects/RTnode.uvoptx"]:
    print("SHA256 " + path + ": " + sha256((ROOT / path).read_bytes()).hexdigest())
print("S03 Build/Link: separate compiler validation required. H01-H10: NOT_RUN (user hardware).")
