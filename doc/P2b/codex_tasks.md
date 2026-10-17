# P2b Codex 执行任务与直接复制提示词

## 编制决策

建议 **1 个 Codex 对话** 实现 P2b 最小功能，避免并行改 `control.c`、`control_policy.*`、Keil工程冲突。不要编辑旧 VisionArm-MCU 仓库。

## 可直接复制的启动提示词

> 你负责 RT-Node F103 的 **P2b LOCAL_FOLLOW 最小实现**，只能修改当前 RT-Node 仓库。用户要求精简，避免无需求的复杂恢复、无关测试、抽象框架。首先检查最新HEAD、git status、实际 AGENTS.md；本包编制时 main=`d11fb8b1de2da1a6c18e55970176979b42b36ce7`，不要假定仍一致，不reset/rebase/覆盖原有文件。
>
> 必读 `doc/P2b/README.md`、`ENTRY_GATE.md`、`requirements.md`、`design.md`、`interfaces.md`、`implementation_plan.md`、`test_plan.md`、`SOURCE_REVIEW.md`，以及 `doc/P2a/P2a_HANDOFF.md`、`App/control/*`、`App/imu/include/*`、`App/imu/src/imu.c`、`App/actuator/include/actuator.h` 和 `Core/Src/main.c`。
>
> P2a 目前只有用户提供约9.8015ms/102Hz采样间隔与238.9µs片选窗口的局部LA截图；没有足够可核验字节与H03/H04证据。**允许P2b编码和主机测试，不得声称P2a已ACCEPTED，不得自行做带载测试。**
>
> 实现新的 `CONTROL_SOURCE_LOCAL_IMU` 和 `CONTROL_LOCAL_FOLLOW`，使 ControlTask (唯一正常执行器所有者) 每20ticks读取 `Imu_GetSnapshot`，仅READY/valid/fresh(age<40ticks)/新generation时产生命令。使用使能瞬间参考Roll/Pitch；初期Roll→Pan、Pitch→Tilt，deadband2°、max±15°、3µs/°、目标各1500±45µs、变化速率≤2µs/20ms；轴向符号由编译配置；不实现Yaw。把映射核心做可主机测试的纯C。
>
> 必须保留 `ControlSession` 一次性授权、terminal不可重新授权、P1现有TTL/generation半环和安全限幅/锁存规则；给LOCAL绑定显式来源授权，TEST老行为不回归。**不得改 App/actuator、TIM3/PC6/PC7寄存器、异常/时钟启动和FreeRTOS port；不得在IMU任务写PWM**。保留ControlTask原20ms/DWT/2ms预算与Enable后≥40ms priming/driver UIF；不修改任务优先级，除非发现真实失败证据并先报告。
>
> 使用精英V2板载 KEY0=PE4（手动一次授权）、KEY1=PE3（正常停止）为**候选实现**，GPIO在ControlTask内轮询、内上拉/低有效、两周期去抖；上电KEY0按住不得自动使能，只在IMU READY后经新按下授权。先核查板卡资源与源码GPIO占用；如有冲突不猜引脚，报告并停在默认禁输出实现。KEY1停止后本次启动无法再授权；IMU/驱动/Control失败按P1安全关闭与terminal。软件失效恢复不得自动运动。
>
> 实现Stage0/Stage1/Stage2独立编译许可：默认Stage0不可输出PWM，Stage1仅无舵机无负载测试，Stage2仅G2证据通过并有操作者明确批准才允许带载。所有输出测试编译要求明确授权宏，并与P1A/P1B测试互斥；运行时还要KEY0，不能以编译宏绕过人工动作。不要建立UART日志、新任务、DMA/FIFO、复杂故障管理、GPIO trace基础设施。新增文件和 Keil工程只做最小改动。
>
> 请完成真实 ARMCC5 Keil Rebuild 和真实生产代码HOST测试：映射限幅限速/NaN、来源与TTL/代际、老TEST回归、KEY0未按不输出、按键授权、KEY1停机且不可重启、失效关断、>=40ms priming。记录完整 build日志、MAP/Flash/RAM/HEX SHA256、修改文件、真实运行结果；不代替操作者伪造硬件LA或机械测试。提供 `doc/P2b/P2b_HANDOFF.md` 和简明 `HARDWARE_TEST_GUIDE.md`；所有未实际做的项目填 NOT_RUN。
>
> **关键停机条件**：若实现必须改变 Actuator 锁存、正常控制全局所有权、一次会话terminal不可重启或绕过P1规则，请停止并提交方案等待总控批准，不要擅改。完成P2b后不自动进入P3。

## Codex 交付验收

- 真实 `main` HEAD/staged/unstaged 差异记录；无改写已有未提交文件。
- 各模块和接口语义一致，能从 README独立复现；默认无PWM编译档可明确确认。
- `BUILD/HOST` 有原始日志；`LA/MANUAL` 没有现场证据就NOT_RUN。
- 汇报尚缺的 P2a门禁、KEY核对与 P2b G1/G2；不得在交接中宣称“带载可测”除非条件成立。
