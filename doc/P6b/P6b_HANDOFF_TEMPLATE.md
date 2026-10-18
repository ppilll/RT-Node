# P6b_HANDOFF（2026-10-18）

- 承接P6a：HEAD `fc427fa7c0c3a0a3e81b17e75096e1bc00c5f6bf` + 上轮未提交P6a工作区，分支main；P6b亦未提交。
- 基线放行：用户本轮声明“测试完成(日志不更新)，接着去完成P6b”，授权继续实施；没有提供逐项实测回执，不补造PASS。旧P6a日志、HEX、MAP及handoff按要求保持原状（172文件内容比对无改动）。
- 实施前核查/修订理由：`source_audit_p6b.md`；最小计划已在修改前说明。
- 本轮生产新增：`App/health/health_runtime.c`、`health_config.h`；修改：`health_app.h`、`Core/Src/main.c`、`App/uart/uart_app.c`、`Projects/RTnode.uvprojx`。P6a `health_app.c`、ControlTask、Flash/BKP流程未改。
- HAL复用已有 `stm32f1xx_hal_iwdg.c`，仅加入Keil，未修改驱动/hal_conf/RTOS配置。

## 实现与关键语义

| 项目 | 实际实现 |
|---|---|
| HealthTask | 静态TCB及256-word栈，prio0，每100ticks（1kHz下100ms），xTaskDelayUntil；只读ControlDiagnostics.wake_count；RuntimeSnapshot短临界段发布 |
| 启动门限 | 首次仅基线；两个连续、有计数变化且间隔至少100ticks的监督区间后才调用HAL_IWDG_Init，一次尝试，不把初始化当无副作用 |
| 停喂门限 | 观察间隔或距最后观察到的Control进展 **>300ticks** 即锁存STALL，此后不因计数恢复而重新喂狗；=300仍允许。100ms采样下通常在400ms无进展处首判停；HealthTask本身被饿死同样无法喂狗 |
| 调度条件 | 每次刷新前确认scheduler RUNNING及进展未超限；不使用外设valid、CONTROL_FAULT或持久锁状态作为生产停喂条件 |
| IWDG | IWDG_PRESCALER_64、Reload1249；标称40kHz为2.00s；LSI30..60kHz推导约2.667..1.333s；均非实测定值 |
| 唯一应用喂狗点 | health_runtime.c/HealthTask内HAL_IWDG_Refresh，当前第146行；Init当前第124行；App/Core各一处，MAP应用调用者也只有HealthTask |
| HAL内部reload | 原HAL Init成功时自身reload一次；未新增直接寄存器reload/ISR/ControlTask等旁路；初始化同一采样不额外Refresh |
| 启动/刷新错误 | HAL前先发布START，成功ON；失败STARTERR，硬件可能已运行，不能报OFF；不重试、不继续刷新，等待已启动的硬件复位 |
| Option Bytes | main在P1a安全输出、Capture之后，HAL/Flash之前只读OBR，IWDG_SW须1且OPTERR须0，不符进入原fatal；无Option Bytes改写或早期喂狗。实机读取 **NOT RUN** |
| 硬件启动配置不符 | 保持直接安全关闭；此时UART未初始化，无法串口诊断，须离线检查配置。没有对硬件自动启动路径作放行结论 |
| 外设/已安全FAULT | OLED/BME/ADC/UART/CAN业务错误不参加监督；Control已FAULT但wake_count变化则继续喂狗，锁定禁用态也正常喂狗 |
| 致命路径 | P1a_FatalError/异常/stack overflow仍先直接关闭，随后不返回自然不能喂狗；软件IWDG启用之前的fatal不受其保护 |
| P6a安全锁 | IWDG复位后仍由原启动代码追加LOCK；普通MODE/CONTROL/后续复位不能清锁；本地恢复LOCK->MANUAL永久禁auto LOCAL |
| 冻结行为 | 本轮无ControlTask/执行器/CAN/KEY0/KEY1/RTOS修改，无新增正常PWM所有者、动态内存、软件定时器、全面心跳或P7 |

实际HEALTH?短字段格式见interfaces.md，最长58B含CRLF。C=INIT/RUN/STALL，W=OFF/START/ON/STARTERR，R/L/P/A为复位/持久锁/策略/本次auto许可，T为编译期测试停喂标志。W=ON表示IWDG初始化成功且硬件已开，不表示当前仍喂狗；生产T恒0。继续使用原UartTask唯一TX、reply[64]、30B chunk及ADC service；两步恢复命令与10s安全条件不变。

## T2专用镜像

- 默认 `P6_WDG_TEST_ENABLE=0`、`P6_WDG_TEST_UNLOADED_ACK=0`。生产不可保留ACK。
- T2须同时ENABLE=1、ACK=1、P3B_OUTPUT_STAGE=0，且历史输出测试全部关闭；错误组合编译拒绝。生产源码预处理分支及MAP无TestCanStop。
- IWDG成功启动至少5000ticks后，仅在非IWDG启动、持久EMPTY/AUTO、Control Disabled/未enabled、Actuator initialized/disabled/无driver fault时锁存停喂（T=1）。没有UART/CAN触发命令。
- LOCK/MANUAL/BAD不能注入；IWDG之后写LOCK，后续NRST/POR也不再注入。不能为了重做T2增加回AUTO旁路。
- 本轮仅编译和主机模拟，**没有下载/执行T2**。运行前必须用户核对独立舵机电源和负载信号断开；任何实际舵机/PWM/带载测试需另行批准。

## 实际构建结果

时间：2026-10-18 18:13（Asia/Shanghai）；ARM Compiler5.06 update7 build960，两次完整Keil Rebuild均exit0、**0 errors/0 warnings**。两工程副本均显式Stage0，原Projects输出及P6a evidence未替换。

| 镜像 | Code / RO / RW / ZI（字节） | MAP LR Size / Max | HEX全部数据地址 |
|---|---|---|---|
| production_stage0/P6b_PRODUCTION_STAGE0.hex | 43694 / 1062 / 248 / 16448 | 0000AFCC / 0007F800 | 45004字节，08000000..0800AFCB |
| t2_stage0/P6b_T2_STAGE0.hex | 43818 / 1062 / 248 / 16448 | 0000B048 / 0007F800 | 45128字节，08000000..0800B047 |

证据均在 `evidence/<variant>/`：rebuild.log、build.json（含精确宏）、compiler_version.txt、生成scatter、Listings/*.map及hex_check.txt。Keil IROM仍为0x08000000+0x7F800；两HEX逐checksum/长度/扩展地址/每数据字节检查通过，末页0x0807F800..0x0807FFFF无数据。编译器版本、尺寸与地址均来自实际新构建，不是P6a旧结果。

## 自检与真实实测分开

| 检查 | 结果 |
|---|---|
| 真实HealthTask主机执行（HAL/RTOS模拟） | **PASS**：基线/两区间启动、停滞锁存且恢复不喂、监督空档、tick/计数回绕、HAL Init失败/过长、Refresh失败、scheduler异常、锁定与安全FAULT继续喂、T2条件与不重注入；runtime_production/runtime_t2日志 |
| UART回归及新格式 | **PASS**：复用原P6a安全解析/确认/超时/回绕/TX失败撤票据测试，新增全reset/policy/C/W组合与58B界限检查；result_uart.log。P6a测试源与日志未动，适配器在P6b evidence |
| P6a Flash/控制/KEY0/KEY1回归 | **PASS**，实际旧测试源，模拟Stage0/1；新结果仅写P6b evidence/host |
| 编译期保护 | **PASS**：无ACK、Stage1、生产残留ACK、非法ENABLE值均按预期编译拒绝；compile_guards.json |
| 源码/链接不变量 | **PASS**：唯一应用Init/Refresh，任务无Flash/PWM操作，UART命令集合未新增，main顺序/安全页及生产剔除T2；source_checks.json；git diff --check通过 |
| P6a旧记录保持 | **PASS**：172文件内容未变/未删除；p6a_preservation.json |
| Option Bytes实机查验 | **NOT RUN**；只读启动门禁已编译，不冒充已读开发板 |
| T1 | **NOT RUN**：无本轮用户实机回执/局部故障观察 |
| T2 | **NOT RUN**：无真实IWDG复位、LSI计时、Flash锁跨POR/NRST或波形证明 |
| T3 | **NOT RUN**：无数分钟联合运行、实机栈余量或截止期测量 |

复现（仓库根目录；仅构建/主机运行）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File doc/P6b/tests/run_host_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File doc/P6b/rebuild.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File doc/P6b/rebuild.ps1 -T2
```

待用户执行的三组简明步骤见test_plan.md：T1正式候选确认C/W与一个已有非关键错误；T2无输出专用镜像确认新启动+R=IWDG、L=1/P=LOCK、NRST/POR锁保持及恢复为MANUAL；T3正式候选数分钟联合运行。测试期间保留安全页，不把mass erase后的重新初始化当锁保持证据。无需新增高频日志、trace GPIO或P7压测。

## 残余边界与状态

- P6a已声明的IWDG-to-Flash总掉电窗口、Flash半写/满页离线维修、mass erase清空状态、UART非认证，以及CPU停滞至IWDG复位之间PWM/独立电源风险仍存在。
- wake_count只证明ControlTask到达循环入口，不证明端到端输出正确；prio0 HealthTask饥饿亦会停喂，需要T3实际验证。256words是分配值，非实机高水位测量。
- 硬件自动启动IWDG不属于放行路径；软件IWDG启动之前仍有未覆盖区间。
- P6b代码及两种Stage0镜像构建已完成；**P6b ACCEPTED：PENDING**，等待真实T1/T2/T3与Option Bytes核查。
- **P6 ACCEPTED：PENDING；未开始P7。**
