# P3b_HANDOFF — 2026-10-17

**BUILD_PASS / AWAITING_OPERATOR_TEST；P3b 未 ACCEPTED，不进入 P4。**

## 基线与范围

- 当前 Git HEAD：`b2c1f34595ae125a3b27f2d5143c4202762a705c`，与文档基线相同。进入时只有用户原有未跟踪 `doc/P3b/`，没有覆盖用户源码改动；未发现适用 AGENTS.md。已完整阅读 P3b 全部入口文档、P3a 冻结接口/历史交接、P2b design/interfaces及相关源码。
- P3a 双端 T1：**USER_REPORTED_PASS；raw evidence not provided**。`doc/P3a/P3a_HANDOFF.md` 的历史 NOT_TESTED 保持原样，本轮按用户后续报告推进。
- 单个 Codex 对话在 Windows/PowerShell 本地集成，所有修改仍在工作区，未提交/推送。未操作板卡、RK3588、下载器或串口。

## 实现与差异

| 文件 | 实际变更 |
|---|---|
| `App/can/can_app.c/.h` | 接入三个Control投递接口、真实STATUS，ControlTask直接消费原ISR overflow；原queue最大阻塞20tick，使控制侧状态变化能触发发送，周期仍100ms |
| `App/control/control.c/.h` | 单套短临界区邮箱、单一裁决、线上state0..7、epoch/时间边界、LOCAL/REMOTE、GLOBAL_STOP/KEY1、正常TTL/overflow停机、中心PRIMING与远程±2µs限速 |
| `App/control/control_policy.c/.h` | REMOTE_CAN=3、Remote授权、正常Disarm；LOCAL/REMOTE可正常重授权，TEST仍单次；fatal terminal保持 |
| `App/control/control_config.h`、新增 `p3b_config.h` | 默认Stage0；Stage1无负载；Stage2需独立许可，旧P1/P2输出测试互斥 |
| `Projects/RTnode.uvprojx` | **未改**，没有新增.c或重复HAL CAN/queue.c；构建脚本临时选择Stage并恢复默认工程/已有Projects输出 |
| `doc/P3b/` | 新增构建脚本、单文件RK测试工具、Stage1操作说明、一个主机C fixture及真实构建/测试证据；接口文档补充实际API |

跟踪源码相对HEAD：7个文件修改（另新增1个配置头）；约507行增加、185行删除。新增文档/证据另计，见工作区 `git diff --stat`。未改 Actuator、LocalFollow、IMU、Driver、HAL、FreeRTOS、Core、startup、CAN时序；CAN的Init/MSP/IRQ/ISR callbacks整段与HEAD逐字一致。

## 冻结协议与关键安全语义

线上协议保持 `0x180/181/182/183/280`、11-bit标准数据帧、DLC8、LE、version1；state0..7/reason/flags未另分配；epoch/seq/action16位半环契约不变。TTL100ms，heartbeat100ms/500ms，STATUS100ms+裁决事件。

- CanTask/ISR不调用执行器。只有ControlTask普通Enable/Apply/Disable，原fatal路径保留。STATUS的state、epoch、enabled、last_action、bit5来自同一完整控制快照，不能把解析成功当作模式批准。
- 正常STOP/模式切换/过期/overflow先Disable，废弃旧授权和目标；新模式产生新epoch。LOCAL需要READY后稳定释放→按下KEY0；REMOTE需要MODE确认后的新epoch、晚于禁用边界的新目标。切换当周期不输出，STOP当周期丢弃普通模式/目标。
- 格式合法GLOBAL_STOP无视旧action，对LOCAL/REMOTE均有效；只向前推进action fence。正常重复STOP幂等；模式重复同内容缓存确认、异内容/旧action拒绝，epoch错误的新action仍消耗fence。单pending不会被新模式覆盖；繁忙拒绝反馈可能合并，RK应串行等待STATUS。
- REMOTE每个新seq产生32位generation，同generation不重复Accept；旧seq、越界、旧epoch、未来/陈旧时间不能续命。心跳只影响bit2。TTL在控制周期与最终Enable/Apply前检查；过期是正常禁用而非EXPIRED fatal。
- 两种模式均保留中心Enable、至少40ms tick+DWT PRIMING、驱动UIF检查；REMOTE每20ms每轴±2µs，最终仍由Actuator限幅。LOCAL复用原映射/限速，不依赖CAN心跳；IMU异常只锁存正在使用它的LOCAL。
- 正常Disarm不清terminal/fault/驱动latch；TEST许可仍一次。硬件fault、deadline、clock、execution继续ForceSafe+terminal，普通STOP/模式/KEY0不能重启。

上述为源码审查和主机决策验证，**不是实机安全性能通过**。原ISR队列中尚未被CanTask解码的STOP受原收帧/调度路径约束；不承诺Bus-Off或中断失效时软件STOP到达。

## 真实构建

2026-10-17 **16:41:30～16:41:45（Asia/Shanghai）**，运行：

```powershell
./doc/P3b/rebuild.ps1 -Stage 0
./doc/P3b/rebuild.ps1 -Stage 1
```

Keil5 / Target 1 / **ARM Compiler 5.06 update7 build960**，两次真实完整Rebuild+链接均退出0、**0 errors / 0 warnings**。

| 构建 | Code / RO / RW / ZI（bytes） | Flash数据 / 静态RAM | 固件 |
|---|---|---|---|
| 默认Stage0 | 21326 / 382 / 140 / 7932 | 21848 / 8072 | [Stage0 HEX](evidence/stage0/P3b_CAN500k_STAGE0_T2_T4.hex) |
| Stage1无负载 | 23598 / 382 / 140 / 7932 | 24120 / 8072 | **[Stage1 HEX](evidence/stage1/P3b_CAN500k_STAGE1_T2_T4.hex)** |

- Stage1 SHA256：`7788EF5EC0BAA770F9A752573A67267779EB57FC674A3B5D638DD541CECEA5B9`。
- 两份HEX记录校验和/EOF/Flash范围通过。Stage1数据范围 `0x08000000..0x08005E37`；Stage0链接确认Actuator_Enable/Apply已移除。详见 [verification.json](evidence/verification.json)。
- 原256word Control栈和CanTask384word栈不变；链接静态最大调用栈估计Stage0=264、Stage1=380bytes（含Unknown提示），不能替代真实栈余量/IRQ/WCET测量。2ms执行预算未放宽。
- 证据：[Stage0日志](evidence/stage0/rebuild.log)、[Stage1日志](evidence/stage1/rebuild.log)、[编译器版本](evidence/stage1/compiler_version.txt)、[Stage1参数](evidence/stage1/build.json)、[Stage1 MAP](evidence/stage1/P3b_CAN500k_STAGE1_T2_T4.map)。Stage0对应文件在其目录中。没有Stage2镜像。

## 实际测试状态

| 项目 | 结果与范围 |
|---|---|
| 真实C主机决策 | **PASS**：Stage0/Stage1各16场景，共32；覆盖无输出、远程启动/限速/范围、重复seq超时、模式重复/忙/错误epoch、STOP抢占、TTL与重授权、overflow、无效目标、REMOTE不依赖IMU、tick回绕、LOCAL离线/按键、模式切换、fatal锁存 |
| 原TEST策略兼容 | 原P1b policy fixture PASS；本轮未广泛重测P1/P2硬件 |
| 编译门禁 | 4组预期拒绝PASS（Stage2缺独立许可、非法Stage、Stage0残留带载许可、旧P2输出）；没有产生带载固件 |
| RK工具离线检查 | Python语法/帮助、模拟STATUS确认与冻结MODE/STOP/heartbeat字节向量PASS；实际Linux SocketCAN **NOT_RUN** |
| T2 Stage1 PWM/LA | **NOT_TESTED**，没有波形或板卡记录 |
| T3 LOCAL/REMOTE实机 | **NOT_TESTED** |
| T4 STOP/TTL/重授权实机 | **NOT_TESTED** |
| 硬件fatal、硬件RX溢出注入 | **NOT_INJECTED**；只有主机决策/代码验证 |
| 实际WCET、ISR响应、运行栈余量 | **NOT_MEASURED** |
| Stage2受限带载 | **NOT_AUTHORIZED / NOT_TESTED** |

主机证据：[results.json](evidence/host/results.json)。`git diff --check`通过。HAL/RTOS/Actuator均为主机stub，任何PASS都不代表真实PWM/UIF或调度性能。

## 操作者下一步与未完成项

1. 按 [STAGE1_TEST_GUIDE.md](STAGE1_TEST_GUIDE.md) 断开舵机电源及信号，只接PC6/PC7/GND逻辑分析仪；明确选择本交接的Stage1 HEX，再按既有方法下载/复位。
2. 在RK3588运行 [rk3588_test.py](rk3588_test.py)：T2中心及±10µs目标；T3运行中切LOCAL/KEY0和无心跳；T4 GLOBAL_STOP、KEY1及仅心跳时TTL。结合 [test_plan.md](test_plan.md) 记录少量candump/LA结果，不能只看STATUS判PWM通过。
3. 目前缺少全部T2/T3/T4实机结果；RK脚本尚未在真实Linux CAN口执行。若2ms预算触发故障，应依据真实现象定位，不直接放宽。
4. 受限带载只有在Stage1实测通过后、用户另外明确批准、确认机械限位与独立物理断电手段后才能准备；本轮未批准、未进行。

| 待用户回填 | 实际结果 | 最小证据 |
|---|---|---|
| T2 | NOT_TESTED | candump + LA截图 |
| T3 | NOT_TESTED | 模式/epoch/按键/离线记录 |
| T4 | NOT_TESTED | STOP/TTL状态与LOW波形 |
| Stage2批准与结果 | NOT_AUTHORIZED / NOT_TESTED | 独立用户批准及现场条件 |

**冻结接口：源码审查遵守；P3b ACCEPTED：NO；进入P4：NO。**
