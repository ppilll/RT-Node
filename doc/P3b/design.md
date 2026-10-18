# P3b 最小技术设计

## A. 当前真实结构（2026-10-17 GitHub P3a commit b2c1f345）

`App/can/can_app.c` 同时持有 CAN HAL、FIFO0 Rx callback、静态 queue、CanTask、五类协议处理和 STATUS；`CanApp_Init()` 由 main 在调度器启动前调用，CanTask 在调度器运行后才Start+启用CAN通知；IRQ priority6、CanTask priority0/384words、static RX queue深8。500kbps硬件链路用户报告T1已通过；P3b不得再建第二个CAN收发实现。现有 `ProcessFrame()`：合法 MODE 全拒、STOP空操作确认、CONTROL合法只统计后丢弃；`MakeStatus()` 只报告DISABLED/epoch0/无远程活性；`#if P2B_OUTPUT_STAGE != 0` 在 P3b 正式路径必须作最小重构。

`control.c` 每20tick ControlTask priority1，DWT/late/2ms预算、P1b条件分支与P2b `#elif` 条件分支；默认P2B_OUTPUT_STAGE0导致原本地执行路径未编入。`control_policy.c` 单次授权，Close任何原因都terminal。`LocalFollow` 单独纯C，无需改算法。`Actuator` 使用 TIM3 PC6/7、50Hz、中位1500，Pan 1000..2000/Tilt 1200..1600，Enable后必须>=40ms PRIMING，再Apply；ForceSafe锁存必须保持。`ImuTask` 10ms、ICM20602/SPI1不变。

## B. 任务/职责（禁止双写者）

`RK3588 SocketCAN 0x180/181/182/183 → STM32 CAN1 ISR(仅有限取帧+原始tick+静态队列) → CanTask(校验/半环序号/写CAN→Control静态邮箱)`。

`ControlTask(20ms)` 每次先处理故障、STOP/KEY1、RX溢出、模式变更，再选择本地IMU或远程CAN目标，单独执行 `Actuator_Enable/Apply/Disable/ForceSafeOutput`；将结果写入`Control→CAN`状态快照。`CanTask` 非阻塞生成`STATUS 0x280`，不可把消息解码成功错当模式切换成功；不可调用Actuator或ControlSession。

使用P3a原来的一条静态CAN RX queue，另增**短临界区完整邮箱**，不新增第二个数据队列。全局STOP事件单独锁存，普通最新目标允许覆盖。Ctrl→CAN状态完整快照非阻塞。共享状态读写需成组一致（epoch与state与reason同一个快照），CAN帧接收时间来自原ISR `received_tick`。

## C. 最小状态转换表（线上state数字固定）

| 当前 | 事件 | 动作 | 下一个状态 |
|---|---|---|---|
| DISABLED | KEY0 READY后有效新边沿 | 确认本地许可，epoch+1，LocalFollow_Start，中位Enable | PRIMING_LOCAL |
| DISABLED / LOCAL_* | 新MODE_REQUEST(REMOTE)且epoch正确 | 先Disable、作废目标、epoch+1、清除远程会话序号基线 | WAIT_REMOTE |
| DISABLED / REMOTE_* | 新MODE_REQUEST(LOCAL)且epoch正确 | 先Disable、作废旧命令、epoch+1，要求新的KEY0 | WAIT_LOCAL |
| LOCAL_*/REMOTE_* | 新MODE_REQUEST(DISABLED) | Disable、旧命令失效，若原不为DISABLED epoch+1 | DISABLED |
| WAIT_LOCAL | 新KEY0授权（READY+valid+fresh） | 采集LOCAL参考、Enable、开始>=40ms启动保护 | PRIMING_LOCAL |
| WAIT_REMOTE | 新epoch/新seq/ISR时刻晚于边界且fresh目标 | Enable中心启动、初始化REMOTE slew | PRIMING_REMOTE |
| PRIMING_LOCAL | 满足>=40ms及DWT/UIF且样本仍fresh | 首次按LocalFollow_Target提交限制后的目标 | ACTIVE_LOCAL |
| PRIMING_REMOTE | 满足>=40ms及DWT/UIF且远程控制仍fresh | 首次按±2µs限速提交目标 | ACTIVE_REMOTE |
| ACTIVE_LOCAL | 新fresh IMU generation | 按原LocalFollow和Actuator处理 | ACTIVE_LOCAL |
| ACTIVE_REMOTE | 新seq新鲜或旧目标仍未超时 | 仅新generation调Accept，旧有效目标按限速继续靠近 | ACTIVE_REMOTE |
| 任一normal / WAIT / PRIMING | 有效GLOBAL_STOP或KEY1 | 优先Disable+清目标+撤销授权，非DISABLED时epoch+1 | DISABLED |
| REMOTE的WAIT/PRIMING/ACTIVE | 已有控制命令 age>=100ms（WAIT且从未有目标无需超时） | Disable、撤销授权、epoch+1、reason=03 | DISABLED |
| 任一非FAULT | 当前CAN接收队列溢出 | Disable、撤销授权、失效旧目标/epoch，reason04 | DISABLED |
| 任一 | 原Actuator fault / deadline / clock / execution / LOCAL必需IMU故障 | 沿用ForceSafeOutput和terminal，不可重授权 | FAULT |
| FAULT | STOP、MODE、KEY0 | 不使能/不清锁存，STATUS维持FAULT | FAULT |

同模式请求只作确认、不刷新epoch和授权；等待本地授权时不能因为CAN心跳消失而自动FAIL。LOCAL模式在未收到任何心跳时也应从KEY0正常启动。退出ACTIVE_LOCAL后重新进入必须再调用LocalFollow_Start，不接续旧roll0/previous脉宽。

**并发优先级**：强制故障 > `REMOTE_STOP`（含较旧action）和KEY1 > overflow > 模式切换 > 新远程控制/IMU本地控制 > 周期反馈。`stop_pending` 必须在消费普通命令前读取并清理，不能先Apply再处理已到达STOP。同周期受STOP约束不得重新Enable；并不承诺CAN中断或总线物理不可用时的硬件急停响应。

## D. ControlSession 生命周期最小调整

原策略的 `authorization_used` 意味每个boot仅一次。P3b经总控批准：**正常DISABLED可重新授权，但fault terminal必须永久禁止直到复位**。采用正常`DisarmNormal`函数：仅在 `!terminal && fault==NONE` 时允许，调用前已经Disable，清`authorized/expected_source/has_generation/last_generation`等本次授权活动字段；不清`terminal/fault/actuator latch`。正常模式切换必须调用它，而不是 `ControlSession_Close()`。`AuthorizeSource` 允许正式LOCAL/REMOTE在一次正常禁用后重新授权，但P1 TEST授权语义仍只使用一次，避免旧P1回归；若需要将`authorization_used`重用于正式模式，必须与TEST专门约束隔离且文档说明。

`ControlSession_Accept` 的来源名单加REMOTE=3、ttl100、范围校验仍在；当应用层已判断为重复CAN seq或无新代际时**不能**调用Accept并触发GENERATION fatal；普通无效帧在CanTask被拒绝，不送到Policy。真正错源、非法范围、违反Policy不变量才走既有fatal路径。正常远程100ms timeout独立于fatal `CONTROL_FAULT_EXPIRED`：在ControlTask调用Policy之前以本地接收时间判断，然后`Disable`进入NORMAL DISABLED，**不得误调用ForceSafeOutput**。

## E. CAN 接口扩展和消除竞态

1. 保留P3a Rx ISR与FIFO0过滤逻辑不变，沿用 `received_tick`；CanTask在消息合法时写最新远程命令完整结构；每个新seq递增内部32bit generation。
2. 模式请求进入单槽pending，等待ControlTask真正裁决。旧P3a“任何合法MODE都拒绝”的逻辑必须替换；STATUS last_action/accepted/reason由控制侧最终结果发布。相同action+相同payload重复不再执行切换；相同action不同内容拒绝；新action但epoch不符消耗action_fence并拒绝。若一个MODE仍未消费又来了新请求，不允许直接覆盖导致状态不一致；可拒绝繁忙请求而不建额外动作队列。
3. 全局STOP格式合法即锁存安全事件，与remote target不同槽。即便旧action也停止；用于后续MODE的action fence仅向前推进。若多个STOP接连到达，输出必须保持DISABLED；不要让随后MODE在同一Control周期重新使能。
4. 模式切换后ControlTask写新epoch+边界接收tick。CanTask可以依据此校验后发布0x180；ControlTask在应用前必须再核对epoch、generation未消费、received_tick不早于/必须晚于有效切换边界及 age<100ms；RESET相关seq基线。RK端应等新STATUS才开始发新epoch命令。
5. STATUS中的所有模式/epoch/actuator/reason字段从Ctrl→CAN快照读取；bit1仍用 `Imu_GetSnapshot`，bit2仍用CanTask的heartbeat，bit4仍读真实ESR。bit5记录最近完成的MODE/STOP结果，非仅报文解码成功；bit3只在远程模式、新epoch、有效年龄<100ms时为1。P3a现有CanTask 100ms queue等待可能延迟状态变化通知：可接受由下一100ms周期发送，或用**一个轻量任务通知**改善事件及时性，禁止为此新增完整TX队列。
6. P3a RX overflow之前只报告reason04，P3b新加ControlTask安全处理；不因无关CAN普通心跳消失停LOCAL。Bus-Off不保证STOP到达，REMOTE通过TTL自动停止；不能承诺永远实时STATUS。

## F. 输出阶段门禁与测试边界

P3a `can_app.c` 当前 `#if(P2B_OUTPUT_STAGE !=0)`会拒绝本地P2b旧测试输出，P3b应改成按**新的正式P3B_OUTPUT_STAGE**（0/1/2）组织：0=正式不输出的默认安全构建/协议状态验证；1=明确人工允许产生PWM但舵机供电+信号断开，仅逻辑分析仪测PC6/7；2=必须用户进一步批准并有独立断电手段才进行有限带载。为避免宏堆叠，最多使用一个stage+最必要的明确stage2许可，Stage0默认无输出；P1A/P1B测试宏与P2b旧测试宏必须保持关闭或互斥。**Codex实现本体、Keil编译和T2-T4 Stage1可准备，不得自行选Stage2并声称实机通过。**

原`Actuator_Enable`自身先输出完整中心脉冲，后续至少等40ms与DWT/UIF；保留原PC6/PC7引脚/50Hz、Pan/Tilt限幅，不要改 `actuator.c` 处理来自CAN的目标。控制任务原2ms执行预算不能通过虚构构建数据声称真实测得WCET；若出现超时要先定位真实阻塞，不直接放宽预算。

## G. 明确不实现

不增加第二套CAN网络/自研底层寄存器驱动；不引入复杂的模式管理框架，不使用全局memset“解除”session，不给CAN ISR增加浮点映射、业务STATE、阻塞TX；不拆分多个并发Codex修改ControlTask/Keil工程；不添加GPIO trace和大量旧阶段测试。此阶段只做能通过T2/T3/T4的必要变更。
