# P6a_HANDOFF（2026-10-18；用户实测待补）

- Git 基线：`fc427fa7c0c3a0a3e81b17e75096e1bc00c5f6bf`；分支 `main`。
- 实际工作提交：未提交，基于上述 HEAD 的工作区修改；原有用户文档保留。
- 实施前核查与最小计划：`source_audit_p6a.md`；修改前已在聊天说明。输入文档实际在 `doc/`，无适用 AGENTS.md。
- 生产新增：`App/health/health_app.c/.h`；修改：`Core/Src/main.c`、`App/control/control.c`、`App/uart/uart_app.c`、`Projects/RTnode.uvprojx`。
- HAL 仅加入已有 flash/flash_ex/pwr 三个源文件，未编辑驱动。P6b 未开发，另两份 handoff 未填写。
- 配套新增本目录测试、构建/HEX检查脚本、证据及核查记录；修订 design/interfaces 并填写 handoff 与模板。

## 安全语义与代码位置

| 项目 | 实现 |
|---|---|
| 安全页/IROM | `0x0807F800..0x0807FFFF` 独占2KiB；Keil Cpu/IROM/OCR_RVCT4 均为 `0x08000000 + 0x7F800`；useFile=0，生成 scatter 的 LR/ER 上限同为0x7F800 |
| EMPTY | health_app.c/FlashScan：全FFFF默认锁定；本地两步及受控软件复位后才追加AUTO=0F0F |
| INVALID/满页 | 未知半字、空洞、非法迁移、满页、写/读回/锁回失败 -> BAD，禁止 UART 恢复，无自动擦除/修复 |
| LOCK | IWDG最高优先追加3333，已有LOCK不重复；包括修订的EMPTY->LOCK，理由见design/source_audit_p6a；此后NRST/软件/POR/PDR均不清锁 |
| AUTO | 人工初次初始化后的这次启动禁auto，后续未故障普通启动保留P5自动机会 |
| MANUAL | LOCK人工恢复追加5555；之后每次复位均无auto LOCAL，仍允许新的有效MODE/CONTROL |
| Boot | main第一项仍P1a_ForceSafeOutput，随后Capture；HAL/时钟就绪后Finalize，早于Actuator/Control/调度器；坏页保留禁用UART诊断路径 |
| Flash | FlashAppend私有，仅BootFinalize调用；拒绝ISR、调度器非NOT_STARTED、已finalized；HAL Unlock/Program HALFWORD/Lock及读回，无应用erase调用 |
| 控制门禁 | control.c：boot_local_pending getter、P3bCycle普通MODE/输出前、最终Enable/Apply前、历史P1b直接测试分支；无第二PWM写者 |
| STOP/fatal | session terminal、KEY0、STOP仍优先；不以Health锁构造新terminal fault；原直接安全关闭不变 |
| 冻结行为 | KEY0 PE4 STOP锁存、KEY1 PE3翻页保留；can_app.c/control.h/control_policy.c/actuator.c/display_app.c均未改，STATUS八字节布局不变 |
| BKP | DR1=6A39、DR2=95C6，原应用无占用；不重置备份域；启动先取票并清除/读回，再判软件复位。SFTRSTF可伴PINRSTF，但排除IWDG/POR/WWDG/LPWR |
| 失败语义 | BKP不可访问/不可清除亦fail-closed；LOCK写失败保留RCC flags供普通复位重试，不声称覆盖总断电窗口 |
| P6b | 没有HealthTask、IWDG Init/Refresh、停喂测试宏或UART/CAN停喂命令 |

原唯一UartTask实现命令：

- `HEALTH?`：RESET/LOCK/POLICY/AUTO/WDG，最大58B含CRLF；reply[64]、30B TX chunk与ADC轮询保留。AUTO表示当前启动的自动LOCAL机会；WDG=OFF仅表示本固件未软件启动IWDG，非Option Bytes实测。
- `SAFE CLEAR?`：仅EMPTY/LOCK、Control Disabled/未enabled、KEY0未锁/PE4高、Actuator initialized/disabled/无fault、无控制fault时READY。
- `SAFE CLEAR YES`：`<10000 ticks`内二次检查后仅写BKP票据；发送SAFE REBOOT成功且第三次安全检查满足后才软件复位。TX/RX错误或条件变化撤销票据。回执不证明Flash恢复成功，必须重启后HEALTH?验证。
- 任意其它完整命令/坏行/RX错误取消窗口，一次YES消耗窗口；LF/CRLF保留，嵌入CR拒绝以免拼接有效确认；本模块不清KEY0锁存。

## 实际构建与离线验证

| 检查 | 结果/证据 |
|---|---|
| Keil Stage0 Rebuild | **PASS**，2026-10-18 17:52（Asia/Shanghai），exit0、0 errors/0 warnings；evidence/stage0/rebuild.log、build.json |
| 编译器 | ARMCC5.06 update7 build960；compiler_version.txt与本次rebuild首行一致；旧P5b日志V6.16不用于本次结论 |
| 生效宏 | 历史P1a/P1b测试OFF；构建副本显式P3B_OUTPUT_STAGE=0；生产头默认0未改；build_project.uvprojx |
| Program Size（字节） | Code=42794，RO-data=1038，RW-data=244，ZI-data=15324；来自本次日志，不推断实机栈余量 |
| MAP | LR_IROM1 Base=08000000 Size=0000AC2C Max=0007F800；ER_IROM1 Size=0000AB38 Max=0007F800；evidence/stage0/Listings/P6a_STAGE0.map |
| HEX隔离 | **PASS**，44076数据字节，范围08000000..0800AC2B；逐条checksum/长度/扩展地址/每个数据字节检查，安全页无数据；hex_check.txt |
| 原P5b主机回归 | **PASS**，实际control/display源码，模拟Stage0/1，含KEY0/KEY1；evidence/host/result_stage0.log、result_stage1.log |
| P6a health/control主机 | **PASS**，空白初始化、EMPTY-IWDG、全部64种reset flags组合、票据消费/撤销、IWDG优先、不重复LOCK、MANUAL跨复位、非法首半字/序列/空洞/满页、HAL失败/读回不符、锁定MODE/Enable/Apply拒绝及STOP/fatal保留 |
| P6a UART主机 | **PASS**，实际uart_app.c解析与reply/reset helper，10s边界/tick回绕、安全条件/二次拒绝、单次确认、精确命令/坏行、TX/RX错误及按键变化撤销、最大58B/chunk服务；result_uart.log |
| 源码不变量 | App/Core无IWDG Init/Refresh/reload；应用仅启动FlashAppend写记录、无erase；冻结文件diff为空；git diff --check通过 |
| 用户实机接口/Flash/PWM | **NOT RUN**：未下载/操作开发板，无用户真实串口、POR/NRST、Flash或逻辑分析仪记录 |

复现（仓库根目录，仅构建/主机运行）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File doc/P6a/tests/run_host_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File doc/P6a/rebuild.ps1
python doc/P6a/check_hex.py doc/P6a/evidence/stage0/P6a_STAGE0.hex
```

构建副本在evidence/stage0，使用绝对源码路径；原Projects输出未替换。主机Stage1仅是假执行器决策测试，没有实机输出。构建事实、源代码推导和实机结果严格分开。

## 待用户实机步骤（全部NOT RUN）

1. 使用Stage0 HEX，断开独立舵机电源和负载信号；核对CH340下载器是否mass erase并记录。本文不授权实际舵机/PWM或带载测试。
2. 空白页启动后HEALTH?应锁定；记录真实回执。普通MODE/CONTROL应被拒绝，STOP/KEY0保持原职责，KEY1翻页。
3. KEY0未按/未锁、无fault、Disabled时依次SAFE CLEAR?与10秒内SAFE CLEAR YES，窗口内勿插其它查询。重启后HEALTH?预期POLICY=AUTO/LOCK=0/AUTO=0。这里只列预期，未伪造回执。
4. 后续普通NRST/断电重启应保留AUTO策略；在无输出条件核对超时/KEY0拒绝。
5. LOCK->MANUAL已有离线验证；**真实IWDG测试NOT RUN，留到P6b专用无输出镜像**。本阶段生产镜像无锁注入/停喂命令，不随意手改安全页。
6. 回传固件、操作、实际回执和结果，用户确认P6a ACCEPTED后才进入P6b。任何实际PWM/机械测试前需另行取得用户批准。

## 已知限制与放行

- **已成功持久化**的LOCK跨完全断电保持；IWDG reset到Flash LOCK写入前再次总断电且RCC/BKP线索消失时旧AUTO可能残留，纯片内软件无法绝对覆盖。独立舵机供电与PWM高阻窗口仍存在。
- UART是本地操作确认，不是身份认证；物理接入/转发主机可模拟命令。
- 半写损坏、页满、BKP访问异常可能报告BAD，拒绝普通恢复，需明确离线维修。mass erase后EMPTY锁定；下载器擦除策略未实测。
- 无运行时Flash擦除/维修、P7压测，也无真实LSI/IWDG/Option Bytes、栈余量、实时截止期或PWM波形结论。
- P6a ACCEPTED：**PENDING**（用户依据实测批准）。
- 是否开始P6b：**PENDING / 尚未开始**。
