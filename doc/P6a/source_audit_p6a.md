# P6a 实施前核查与修订记录（2026-10-18）

事实基线：HEAD `fc427fa7c0c3a0a3e81b17e75096e1bc00c5f6bf`，分支 `main`。开始时只有用户提供的 P6 文档为 untracked，无既有源码改动。根目录无 README/DECISIONS 文件，本次输入实际为 `doc/README.md`、`doc/DECISIONS_AND_LIMITS.md`、`doc/references/source_audit.md` 及完整 `doc/P6a/`、`doc/P6b/`。未找到适用的 AGENTS.md。

| 修改前源码事实 | 证据 | 实施决定 |
|---|---|---|
| main 首项是 P1a_ForceSafeOutput；HAL Init/时钟后初始化模块，最后共享 SysTick 交接 | Core/Src/main.c | 安全动作后 Capture，时钟就绪后 Finalize；错误保留锁定诊断路径 |
| SysTick 同时服务 HAL/RTOS；所有异常和 RTOS assert/stack overflow 直接 fatal | Core/Src/interrupts.c、rtos_hooks.c | 不修改 |
| boot_local_pending=true；MODE 独立授权；Enable/Apply 有最终 KEY0/overflow/TTL 检查；P1b 另有直接测试出口 | App/control/control.c | 启动 getter、MODE gate、Enable/Apply gate、P1b gate；无新控制状态机 |
| ControlSession 的 source/generation/TTL/terminal 规则及 driver direct-safe 已存在 | control_policy.c、actuator.c | 不修改，health 不持有 PWM 写权限 |
| Control 20 ticks、TTL100、执行2000us、prio1/256 words；wake_count 每轮入口增加 | control_config.h、control.c | 不修改，P6b 尚未使用 wake_count |
| KEY0 只初始化 PE4，KEY1 只由 DisplayTask 使用 PE3；healthy 为 OLED 局部变量 | control.c、display_app.c | 均不修改，未增加 OLED 健康 getter |
| IMU/CAN/UART/ENV/Display 静态 prio0；ADC 在 UART 轮询；非关键故障局部处理 | imu.c、can_app.c、uart_app.c、adc_app.c、env_app.c、display_app.c | 保留，无全面心跳/新任务 |
| CAN STATUS DLC8 与字节布局冻结 | can_app.c | 文件未修改，拒绝 MODE 沿用 reason=0x05，不增协议字段 |
| UART 唯一 TX；line32、reply64、每块30字节、HAL_UART_Transmit 等 TC | uart_app.c、HAL UART 驱动 | 原任务内精确维护命令；最大 HEALTH 58B；成功发送后复位 |
| P3B_OUTPUT_STAGE 默认0；Keil 无覆盖；历史测试宏均关 | p3b_config.h、RTnode.uvprojx | 保持，构建副本额外显式 stage0；主机 stage1 仅模拟函数 |
| HAL FLASH、PWR、IWDG 宏已开；512KiB/2KiB；HAL Program 半字/读回可用 | hal_conf.h、stm32f103xe.h、hal_flash.c | HAL Flash/PWR 可直接复用，不改 HAL 实现 |
| HAL FLASH IRQ 调用 FlashEx 中的 FLASH_PageErase；BKP API 在 PWR 源码；这三个驱动均未编入工程 | hal_flash.c/.h、hal_flash_ex.c、hal_pwr.c、uvprojx | 加入 Flash/FlashEx/PWR；应用无 erase 调用，链接器移除未用 erase |
| App/Core/Startup 无 DR1/DR2 占用、尾页自定义对象或旧 Flash 存储；无自定义 scatter | 全目录限定搜索、uvprojx useFile=0/umfTarg=1 | DR1/DR2 专用票据；IROM/Cpu/OCR_RVCT4 同步缩小 |
| 工程 pCCUsed=ARMCC5.06u7、uAC6=0；本机 armcc --vsn=5.06u7 build960 | uvprojx、evidence/stage0/compiler_version.txt | 实际执行 Keil Rebuild；不把旧日志标签当编译器版本证据 |

文档与证据差异及理由：

- 原迁移候选表遗漏 EMPTY->LOCK；已修订 design.md。首次初始化前检测到 IWDG 也必须持久记 LOCK，之后只能 MANUAL。
- 原 source_audit 只列 FLASH 缺项，实际还需 FlashEx 的链接依赖及 PWR 的 BKP 访问函数；工程加入三者，IWDG 保留未编入。
- 旧 `Projects/P5b_build.log` 首行显示 `V6.16` 标签，与当前工程的 ARMCC5 选择及可执行文件自报不一致。本次新 `evidence/stage0/rebuild.log` 首行明确为 `V5.06 update 7 (build 960)`；按新构建证据报告，不用旧标签推断本次编译器。
- UART 原先吞掉行内任意 CR；维护命令收紧为只接受 LF/CRLF，防止杂字符被拼成有效 YES。普通命令名称、接口及原单任务 TX/ADC service 不变。
- 写 LOCK 失败时不清 RCC flags，保留跨普通复位重试的证据；坏页不能靠追加 MANUAL 修复。BKP 不可访问/不可清除也 fail-closed。

变更前已在聊天说明最小范围：新增 health 两文件，修改 main/control/uart/Keil 四文件，加离线验证与交接文档。没有改 actuator、control_policy、CAN、KEY0/KEY1、HAL 驱动、RTOS 或 P6b。

构建与实机结论分别见 P6a_HANDOFF.md。源代码推导和主机模拟均不等于 Flash 供电可靠性、真实调度时限或实机 PWM 证据。
