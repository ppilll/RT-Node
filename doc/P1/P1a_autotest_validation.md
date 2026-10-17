# P1a 自动测试入口交付记录

日期：2026-10-16，Asia/Shanghai。基线 HEAD：`7588842c29bfc0d1f5fce3011c727b4df38fc3d5`。

实现已完成；硬件测试 **NOT_RUN**，P1a Gate 不因此升级。自动测试只允许断开舵机信号和动力的无负载波形采集。

## 变更文件

| 文件 | 变更 |
|---|---|
| `App/p1a_test/p1a_test.h` | 默认关闭的自动入口宏、CASE 选择、互斥及非法值编译检查 |
| `App/p1a_test/p1a_test.c` | 三个一次性序列、返回值/快照断言、有界等待、只读 TIM3 边界选择 |
| `Core/Src/main.c` | InitSafe 成功后，在编译开关下调用一次自动入口；原首次关断及默认 WFI 保留 |
| `doc/P1/C2_hardware_guide.md` | CH340/RESET 操作、宏表、逐段目标/时长/顺序、证据限制 |
| `doc/P1/test_plan.md` | 自动 CASE 与 H 项的部分覆盖关系 |
| `doc/P1/C2_static_check.py` | 保留原 Mailbox 检查，增加自动入口检查，纠正旧 Build/Link 未执行原因 |
| `doc/P1/P1a_autotest_check.py`（新增） | ARMCC5 独立编译/链接矩阵与负向宏检查，可重复执行，不生成第二套 Keil 工程 |
| 本记录（新增） | 构建结果、边界和遗留项 |

`App/actuator/include/actuator.h`、`App/actuator/src/actuator.c`、安全关断/异常处理实现均无 diff。六个 actuator API、PC6/PC7 映射、50 Hz、1 µs、Pan 1000–2000 / Tilt 1200–1600 和故障锁存语义保持原实现。

开始工作时 `Projects/RTnode.uvprojx`、`.uvoptx` 已修改，并存在未跟踪构建文件。本次没有改动这些文件，没有覆盖 `Projects/` 构建产物，没有修改旧仓库，没有创建分支、提交或推送。

工程文件开始/结束 SHA256 相同：

```text
Projects/RTnode.uvprojx 98ec1c961c0e5d147a37df1073fa86be4c83ba9408013d798e84859661a9e272
Projects/RTnode.uvoptx  f932b03e4834c3c65397f022f8bb64277f80e16c68f25dbe3ec50d15e181a52a
```

## 已执行的验证

本机实际工具：`D:\MKD5.36\ARM\ARMCC\bin`，ARM Compiler **5.06 update 7 (build 960)**，Cortex-M3 Thumb，ARMCC 默认 C 语言模式，分别 `-O0` / `-O2`。没有依赖 ARMCLANG 扩展；现有工程当前选 ARMCLANG 6.16，本次不替用户改选编译器。

编译同一工程现有的 9 个 C 文件及 `startup_stm32f103xe.s`，链接地址 Flash `0x08000000`、RAM `0x20000000`、入口 `Reset_Handler`、向量在前，输出独立 MAP/AXF/HEX。它验证实际源文件的 ARMCC5 兼容性；没有启动 Keil IDE 重建，也不宣称与用户工程全部选项逐项相同。

| 检查 | 结果 |
|---|---|
| 默认（完全不覆盖测试宏，使用头文件默认）× O0/O2 | PASS：2 次编译/链接 |
| 原 Mailbox × O0/O2 | PASS：2 次编译/链接 |
| 自动 CASE 1/2/3 × O0/O2 | PASS：6 次编译/链接 |
| 上述 10 个有效组合 | 编译/链接无错误、无警告；HEX 转换成功 |
| 默认链接符号 | PASS：没有 Enable/Apply、自动入口或 Mailbox 符号 |
| Mailbox 链接符号 | PASS：存在 Mailbox，无自动入口 |
| 三个自动 CASE 链接符号 | PASS：存在自动入口，无 Mailbox |
| 同时开启两入口 | PASS：预期 `#error`，拒绝编译 |
| ENABLE=2（分别测试两宏）、CASE=0、CASE=4 | PASS：4 个预期编译拒绝 |
| `python doc/P1/C2_static_check.py` | PASS：默认关断、API 数量、寄存器路径、有限等待与同步更新模型等静态检查 |
| `git diff --check` | PASS |
| Keil IDE 项目 Rebuild、当前 ARMCLANG 项目完整构建 | NOT_RUN：保留用户工程配置与已有产物，执行了上述 ARMCC5 独立构建 |
| 固件运行、Tick/DWT/TIM3 故障注入执行 | NOT_RUN：没有模拟执行 MCU，也没有下载运行 |
| CH340 烧录、RESET、逻辑分析仪、H01–H10 | NOT_RUN：需要用户实机证据 |

复现命令（从仓库根目录运行）：

```text
python doc/P1/C2_static_check.py
python -u doc/P1/P1a_autotest_check.py --out <独立验证输出目录>
git diff --check
```

本次验证产物目录（独立于原工程产物）：

```text
C:\Users\Liu\.codex\visualizations\2026\10\16\01a144b5-e3c0-76b0-a45c-c82dafc76db3\P1a_validation
  build.log                     完整命令、编译/链接日志（末尾含预期负向编译错误）
  static_check.log              静态检查与源码 SHA256
  manifest.json                 交付源码及各 AXF/HEX 的 SHA256
  P1a_autotest.diff             仅本次 8 个文件的完整 diff，含新增文件
  default_O0 / default_O2       默认固件 MAP/AXF/HEX
  mailbox_O0 / mailbox_O2       原邮箱固件 MAP/AXF/HEX
  auto1_O0 / auto1_O2           CASE 1 MAP/AXF/HEX
  auto2_O0 / auto2_O2           CASE 2 MAP/AXF/HEX
  auto3_O0 / auto3_O2           CASE 3 MAP/AXF/HEX
```

排除工作前既有工程改动的源码 diff 命令：

```text
git diff -- App/p1a_test/p1a_test.h App/p1a_test/p1a_test.c Core/Src/main.c doc/P1/C2_hardware_guide.md doc/P1/test_plan.md doc/P1/C2_static_check.py
```

此命令不显示未跟踪的两个新增文件；完整补丁已把它们加入。当前工作区总 diff 还会显示用户已有的两项工程改动，不能把它们算作本次变更。

## 安全审查与遗留项

自动入口仅调用现有 API 改变输出，不写 TIM3/GPIO/AFIO；DWT 配置只打开 trace/cycle counter，不清零计数。默认两入口都为 0。CASE 1/2 成功返回主循环后保持禁止，CASE 3 在锁存后验证 Enable/Apply/InitSafe 的预期拒绝，最后 Disable 仍保留故障。所有非预期结果进入原 `P1a_FatalError()`，先关断再安全停留。

等待同时要求 DWT 和 HAL Tick 达到最短时长，并核对两者偏差；每个里程碑有 65536 次 CPU 轮询上限，外层有确定步数。TIM3 计数与 DWT 做相位一致性检查。边界测试有 25 ms 与 2097152 次轮询双重上限。即使 Tick 停止或两个计时源同时停止，CPU 仍运行时也不会留在无限等待。初始化阶段沿用原 HAL 路径，PWM 尚未使能。不是独立看门狗，无法对 CPU 完全停止作出软件关断保证。

仍需用户验证：

1. 三个 CASE 的完整实际波形、首个完整中位、脉宽与周期容差、Disable/故障截断、恢复无旧目标重放。
2. DWT 在实际板卡独立运行时可用，Tick 和 TIM3 一致性阈值适合当前固件；不通过时应定位原因，不能去掉检查或清锁存。
3. CASE 2 只保证在接近溢出的 CNT 窗口发起调用，不能保证 API 内部写 CCR 的精确边界相位。须保存双通道瞬态；覆盖不足仍为 PARTIAL/NOT_RUN。
4. 仅分析仪不能证明最终低电平代表所有内部断言通过；没有内部读回的部分保留证据缺口。自动脚本不替代完整 H01–H10，更不能用于 H10 带载测试。
5. 每次烧录前在当前唯一 Keil 工程一致设置全局宏并 Rebuild；测试结束重新刷回两入口均为 0 的默认固件。自动固件再次 RESET 会重新执行脚本。
