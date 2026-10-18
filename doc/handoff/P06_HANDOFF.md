# P06_HANDOFF（2026-10-18）

## 版本及变更

- P5基线：`fc427fa7c0c3a0a3e81b17e75096e1bc00c5f6bf`，main。
- P6a/P6b均为此HEAD之上的未提交工作区；未伪造阶段提交号。
- P6a：见 `doc/P6a/P6a_HANDOFF.md`。用户本轮声明测试完成并授权继续P6b；按“日志不更新”要求，旧P6a记录保持原样，不把未提供的逐项回执补成PASS。
- P6b：见 `doc/P6b/P6b_HANDOFF.md`；新的构建/主机证据在P6b evidence，实机T1/T2/T3仍NOT RUN。
- 主要文件：App/health/health_app.c/.h（启动持久锁）、health_runtime.c/health_config.h（监督/IWDG）；main、control（仅P6a改）、uart、Keil工程。HAL驱动仅加入原Flash/FlashEx/PWR/IWDG，不改驱动源码。

## 实现摘要

1. 启动不可变BootSnapshot与任务只读RuntimeSnapshot；HEALTH?以C/W/R/L/P/A/T短字段报告，最长58B，原USART1唯一TX不变。
2. Flash末页0x0807F800..0x0807FFFF，IROM缩为0x7F800；半字追加AUTO=0F0F、LOCK=3333、MANUAL=5555；空白/损坏/满页fail-closed，不自动擦除，只在调度前写。
3. 初次明确本地初始化EMPTY->AUTO，当前恢复启动无auto；IWDG优先记LOCK（包括EMPTY->LOCK）；恢复LOCK->MANUAL，随后任何系统复位都禁自动LOCAL，只能新有效MODE/CONTROL。
4. SAFE CLEAR?/SAFE CLEAR YES，10s及Disabled/KEY0/执行器/fault条件；BKP票据先消费清除，受控软件重启才追加恢复记录；SAFE REBOOT不是持久恢复证明。
5. P1a安全动作后保存RCC原始flags；已验证持久状态后清flags，LOCK写失败保留证据供普通复位重试。完全掉电丢证据窗口仍保留。
6. main在HAL/Flash前只读检查OBR软件IWDG/无OPTERR；不符原fatal直接关闭，不进行早期无条件喂狗或Option Bytes写入；此配置分支UART不可用，实机选项字节查验NOT RUN。
7. HealthTask静态prio0/256words、100ticks，只监督ControlTask原wake_count；两个连续有效区间后启IWDG64/1249。名义2s，LSI30..60kHz推导1.33..2.67s，无真实计时。
8. HAL_IWDG_Refresh只在HealthTask一处，调用前检查调度/进展；超过300ticks观察空档或无进展锁存停喂。已安全FAULT和外设局部错误不单独停喂；HAL启动失败STARTERR不误报OFF。
9. ControlTask仍为唯一正常PWM写者；KEY0 PE4 STOP锁与KEY1 PE3翻页及冻结CAN不变；fatal/异常/stack overflow始终先直接关闭，IWDG启用后不返回路径自然不能喂狗。
10. T2仅显式ENABLE+UNLOADED_ACK+Stage0且无历史输出测试；启动IWDG5s后、Disabled与EMPTY/AUTO等条件下锁存停喂；LOCK/MANUAL/BAD及IWDG启动不注入，生产无该函数或远程触发命令。

## 真实构建及自检

| 项目 | 结果 |
|---|---|
| P6a Keil | 上轮实际ARMCC5.06u7构建0 errors/0 warnings，旧证据保留 |
| P6b Keil生产Stage0 | **PASS**，2026-10-18 18:13，ARMCC5.06u7 build960，0 errors/0 warnings；Code43694/RO1062/RW248/ZI16448 |
| P6b Keil T2 Stage0 | **PASS**，0 errors/0 warnings；Code43818/RO1062/RW248/ZI16448 |
| 两HEX安全页隔离 | **PASS**，生产最高0800AFCB，T2最高0800B047；生成scatter/MAP Max=0007F800，逐HEX数据检查安全页无数据 |
| 主机/静态自检 | **PASS**：启动/停滞/回绕/错误与安全FAULT喂狗规则、T2保护、UART恢复与长度、P6a Flash/控制/按键回归、唯一喂狗点与生产剔除T2；均为模拟/源码结论 |
| 用户P6a测试 | 用户声明完成、授权作为继续基线；未提供逐项新日志，旧记录不更新 |
| Option Bytes实机、P6b T1/T2/T3 | **NOT RUN**，未下载、未操作开发板、未取得真实回执/波形/计时 |

P6b交接文件提供两HEX、复现命令及test_plan.md三组精简步骤。运行T2前核对舵机独立电源/负载信号断开；生产构建默认Stage0及测试宏0。任何实际PWM/机械/带载测试尚未授权，须用户另行批准。

## 残余风险

- IWDG后到Flash LOCK前再次总断电并丢RCC/BKP证据，纯软件无法绝对覆盖；只保证已持久化锁跨断电保持。
- Flash半写损坏/页满需离线维修；mass erase后EMPTY锁定、重新本地初始化，不能拿重刷后结果证明跨复位锁保持。
- UART两步确认不具密码学身份认证。
- ControlTask失去监督至IWDG复位间PWM及独立舵机供电物理风险未消除；wake_count仅循环入口证据，HealthTask低优先级饥饿也可能复位。
- 软件IWDG启动前无狗覆盖；硬件自动启动配置被拒绝且无UART诊断。栈余量、deadline、LSI与实际复位时序均未实测。

## 接受及后续

- P6a基线：用户已授权继续（旧handoff按要求不回写，逐项实测报告未新增）。
- P6b ACCEPTED：**PENDING**，待实际Option Bytes/T1/T2/T3。
- P6 ACCEPTED：**PENDING**。
- P7：**未开始，不自动进入**。
