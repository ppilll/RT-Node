# P6b 核查与实施记录（2026-10-18）

承接工作区P6a，Git HEAD仍为 `fc427fa7c0c3a0a3e81b17e75096e1bc00c5f6bf`，无P6a独立提交。用户本轮明确“测试完成(日志不更新)，接着去完成P6b”，作为继续实施授权；不补造P6a实测细节，不回写旧日志/交接。

修改前重新核查P6a实际源码/交接、P6b全套文档以及HAL IWDG与Option Bytes定义；在聊天说明最小计划后再改代码。

| 核查事实 | 证据及实施 |
|---|---|
| P6a安全锁、先安全输出再Capture、启动Flash追加、双层控制门禁均已存在 | health_app.c/control.c/main.c；不改持久锁或ControlTask |
| HAL IWDG Init先START，再写PR/RLR，等待SR，超时也可能已经开启，成功内部reload | Driver/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_iwdg.c；复用驱动，START状态先发布，失败不再喂狗 |
| HAL_IWDG_MODULE_ENABLED已开，驱动尚未加入工程 | hal_conf.h/uvprojx；只加入原iwdg.c |
| OBR IWDG_SW是bit2，OPTERR是bit0；不是直接拿OB_IWDG_SW=1做OBR掩码 | stm32f103xe.h；main只读检查，在HAL/Flash前拒绝不符配置，不改选项字节 |
| Control_GetDiagnostics是短临界段快照，wake_count在每轮入口增加 | control.c；生产监督仅该计数，不检查外设valid或mode/fault，不新增控制心跳 |
| 任务全部静态；1kHz/32bit tick、delayUntil可用、软件定时器禁用 | FreeRTOSConfig.h；新增prio0/256words/100ticks静态任务，不改RTOS配置 |
| 原UART reply64、30B块、单TX与ADC轮询 | uart_app.c；仅替换HEALTH?字段构造，最大58B，本地恢复逻辑保留 |
| 原IROM三处0x7F800，Flash页预留正确 | uvprojx；不改IROM；两新HEX逐数据字节确认末页无数据 |
| P1a fatal/stack overflow直接关闭在先 | main.c/interrupts.c/rtos_hooks.c；原关闭路径不变，IWDG开启后非返回故障循环自然不能刷新 |

生产文件改动仅六项：新增 `App/health/health_runtime.c`、`health_config.h`；扩展 `health_app.h`、`Core/Src/main.c`、`App/uart/uart_app.c`、`Projects/RTnode.uvprojx`。P6a `health_app.c`、control.c以及CAN、执行器、KEY0/KEY1、RTOS未在本轮修改。

设计补充已明确写入design/interfaces：硬件IWDG配置阻断及无UART诊断代价；START中间态；>300ticks空档先判再收新计数且停喂锁存；T2只允许EMPTY/AUTO，并以LOCK/MANUAL禁止跨NRST/POR重复注入。没有静默更改安全行为。

P6a目录172个文件在本轮前后做内容比对，均未修改/删除；报告位于 `evidence/host/p6a_preservation.json`。新编译与模拟测试记录全部在P6b evidence，不能等同于用户实机T1/T2/T3。放行状态见P6b_HANDOFF.md。
