# P2b交接 — IMPLEMENTED / HARDWARE NOT_RUN（PARTIAL）

2026-10-17，Codex单会话，仅当前仓库。**P2b ACCEPTED=NO；G2未批准；P3=NO。**

## 1. 身份与修改

- 进入/交接HEAD：`d11fb8b1de2da1a6c18e55970176979b42b36ce7`，main；HTTPS git ls-remote核对远端main相同。实现未提交、未推送。
- 进入时只有未跟踪`doc/P2b/`；原输入文档保留，追加实际接口/源码审查，完善交接及硬件指南。仓库和适用父目录没有AGENTS.md。
- 修改生产文件：`App/control/{control.c,control.h,control_config.h,control_policy.c,control_policy.h}`；新增`App/control/{local_follow.c,local_follow.h,p2b_config.h}`；`Projects/RTnode.uvprojx`只添加local_follow.c。
- 新增交付：本文件、`tests/{local_test.c,controller_test.c,stubs/stm32f1xx_hal.h}`、`run_host_tests.py`、`rebuild.ps1`、`check_static.py`、`evidence/`；更新`interfaces.md`、`SOURCE_REVIEW.md`、`HARDWARE_TEST_GUIDE.md`。
- 328个受保护已跟踪文件相对HEAD不变：Actuator、P2a IMU/Driver、main/异常/时钟/startup、HAL/CMSIS、FreeRTOS/port、原P1测试源。生产源码SHA256清单及变更见`evidence/static.json`、`source.diff`。

## 2. 实际行为

- ControlTask仍为唯一正常执行器所有者，priority1、20ticks、256words栈；ImuTask priority0不变。LOCAL_FOLLOW读取完整IMU快照，READY/valid/有限角/age<40且新generation才接受命令；issued_tick=sample_tick，TTL=40。无Yaw。
- TEST=1保留，新增LOCAL_IMU=2、AuthorizeLocal和会话expected_source。原Authorize绑定TEST；混用/未知来源拒绝，原TTL/generation半环/范围/锁存规则保持。
- authorization_used与terminal保持一次性：KEY1正常Disable+COMPLETE；输入、代际、驱动、迟到、DWT或2ms预算失败ForceSafe锁存+terminal。通信恢复、再次按键或新generation均不能自动重启，无运行时解锁/恢复API。
- 授权时参考Roll/Pitch，Roll→Pan、Pitch→Tilt；含边界2°死区（归零，不扣2°）、±15°限角、3µs/°、1500±45µs目标、每次最多2µs，偏移向零截断。编译P2B_PAN_SIGN/P2B_TILT_SIGN独立±1，默认+1。避免跨±180°跳变，先近水平授权。
- KEY0 PE4、KEY1 PE3为候选，内上拉/低有效、ControlTask内两次连续采样去抖。只在READY后两次松开→两次按下授权，boot长按/READY前按住无效；KEY1优先，未授权时按KEY1也terminal。资料与源码未发现冲突，**实物接线/电平NOT_VERIFIED**，烧录Stage1前人工确认。
- Enable后首次Apply保留≥40ticks且DWT≥2,880,000cycles，原driver UIF检查不变。priming不推进映射last_output，首个目标最多离中位2µs。真实硬件低帧/UIF尚未测。
- 默认Stage0无LOCAL GPIO/Enable/Apply路径，MAP确认移除Enable/Apply。Stage1须编译许可且仍需KEY0；Stage2另需G2/操作者ACK，本轮未构建。P1A/P1B与P2b输出互斥，未绕过P2a整机P1测试禁令。
- 无新任务、日志、trace、DMA/FIFO、队列或故障管理框架。未触发需改变P1锁存/所有权/terminal的停机条件。

## 3. 真实构建与镜像

Keil `-r` Rebuild，`Projects/RTnode.uvprojx`、Target 1；实际ARMCC **5.06 update7 build960**（日志V6.16为历史工具别名）。完整版本/日志、临时构建工程、MAP、AXF、HEX、callgraph、link参数与hash保存于`evidence/armcc/stage0/`及`stage1/`。

| 镜像 | Rebuild | Code/RO/RW/ZI bytes | Flash=Code+RO+RW | RAM=RW+ZI |
|---|---|---|---:|---:|
| Stage0默认 | PASS，0 error / 0 warning | 14274 / 382 / 112 / 5800 | 14768 | 5912 |
| Stage1无负载专用 | PASS，0 error / 0 warning | 16162 / 382 / 112 / 5800 | 16656 | 5912 |
| Stage2 | NOT_RUN，无G2批准 | NOT_RUN | NOT_RUN | NOT_RUN |

Stage1多1888bytes Flash，静态RAM不增。ControlTask静态栈仍1024bytes；Stage1 callgraph自身168bytes、Max Depth248bytes，仅静态报告，不证明真实运行栈余量/WCET。

HEX SHA256：

- Stage0 `evidence/armcc/stage0/RTnode.hex`：`e81048f355942ea6fc7d66eb4bf7fe9074715e5311fc8546c0218b2557054867`
- Stage1 `evidence/armcc/stage1/RTnode.hex`：`711e57eb71ddc83470f5a62cd5a0cdb35872f23a0a211afcbf537b7b162eaff1`

MAP SHA256：Stage0 `452b3ff74a5d78e43fb953d762401f016bf2527589aa52aa09db36c9990a5201`；Stage1 `d4a92b2790b0c4291e58888e936184a05355d81ba42a84399366e5116c1e2777`。AXF hash见各`hashes.json`。

默认工程定义：P1A_ACTUATOR_TEST_ENABLE=0、P1A_AUTOTEST_ENABLE=0、P1A_AUTOTEST_CASE=1、P1B_AUTOTEST_ENABLE=0、STM32F103xE、USE_HAL_DRIVER；P2B_OUTPUT_STAGE默认0，无输出许可。Stage1仅追加`P2B_OUTPUT_STAGE=1,P2B_UNLOADED_ACK=1,P2B_OUTPUT_ARM_KEY=0xB2201500UL`。Rebuild脚本恢复默认工程/已有输出，交付HEX仅在evidence；未下载或启动板卡。Stage1构建通过不表示G1允许烧录。

## 4. 实际验证

| ID | 结果 | 证据/边界 |
|---|---|---|
| S01 STATIC | PASS | static.json、source.diff；328受保护文件不变、默认MAP移除Enable/Apply、git diff --check通过 |
| B01 BUILD | PASS | Stage0/1真Keil/ARMCC5完整日志，0 errors/0 warnings；Stage2 NOT_RUN |
| U01 HOST | PASS | 正负方向、参考/独立轴、死区、绝对目标不积分、限幅/限速、NaN/Inf及差值溢出 |
| U02 HOST | PASS | 来源绑定/混用/未知、TTL39/40/回绕/未来、代际重复/旧/半环/回绕、范围、terminal；原policy fixture未改 |
| U03 HOST | PASS | 无键/boot长按/READY前按住无输出、READY后新按/两键/去抖、KEY1停且不能重启、失效/驱动/时序关断及恢复不重启、双时钟priming |
| H01 LA上电/按键/首帧 | NOT_RUN | 未烧录，无现场捕获 |
| H02 LA映射/范围/速率 | NOT_RUN | 无现场捕获 |
| H03 LA缺失/失效关闭 | NOT_RUN | 无现场捕获，无新增硬件注入接口 |
| H04 MANUAL带载 | NOT_RUN | G2未批准、Stage2未构建 |
| 真实WCET/运行栈余量 | NOT_MEASURED | 无独立真实读出 |

MSVC `/W4 /WX /O2`直接编译执行生产control.c/control_policy.c/local_follow.c，HAL/RTOS/Actuator仅stub。完整命令/返回码/输出在`evidence/host/results.json`。**53次HOST执行通过**（2映射方向、1原policy、6 Stage0、29 Stage1、15历史TEST），另12组未授权/互斥负向编译按预期拒绝。历史TEST原fixture未改，四个有限用例及TTL/迟到/执行超时/既有故障回归通过；仅隔离HOST，未生成P1整机测试镜像。

HOST首次Apply名义40ticks；DWT故意少1ms的case在40ticks仍不Apply，60ticks才首次更新且仍限2µs。HOST只验证调用条件，不模拟或修改生产Actuator寄存器，不冒充真实UIF/40ms/PWM测量。首轮负向测试遇P1a既有守卫先报错，已修正期望文本；最终全量结果以保存的results.json为准。

## 5. 后续门禁

G0软件通过；G1现场前置未完成（实物KEY、P2a关键证据、无负载授权/接线及波形尚缺）；G2未批准。P2a H01/H02/H03/H04本轮无新增证据，保留历史NOT_RUN/入口NOT_VERIFIED，不改旧交接。操作者按[HARDWARE_TEST_GUIDE.md](HARDWARE_TEST_GUIDE.md)补齐记录后再评审，不能直接接舵机。复现命令见该指南；不自动进入P3。
