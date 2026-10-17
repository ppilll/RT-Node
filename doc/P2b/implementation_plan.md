# P2b Codex 最小实施顺序

## 进入与审查

1. 重新获取最新 Git HEAD/status，不覆盖未提交工作；阅读本包 README/ENTRY_GATE、P1源、P2a源码、`doc/P2a/P2a_HANDOFF.md`、`doc/P1b/interfaces.md`。
2. 查仓库是否有实际生效 AGENTS.md，遵守；记录当前 P2a H01–H04 的来源与实际状态。不把用户截图升级为全通过。
3. 读取正点原子精英V2板载按键资源，结合当前 GPIO 使用检查 KEY0 PE4/KEY1 PE3、电平和内上拉；若不符合停止输出相关集成并报告替代方案。默认编译 Stage0。

## 代码实施（建议同一个 Codex 会话完成，不并行）

1. `local_follow` 纯 C 算法 + HOST tests：相对参考姿态、死区、±15度输入限制、3µs/deg、±2µs/20ms rate limit、1500中位、方向编译常量、有限数检查。
2. `control_policy` 最小 LOCAL_IMU来源鉴权：保留 TEST API/行为，新增期望来源；保留 generation/TTL/range/terminal 规则并扩展 HOST 负向测试。
3. ControlTask 最小状态逻辑：默认无输出、KEY0在READY后手动 arm、KEY1停止、IMU状态与40tick新鲜度/来源检查、40tick priming、Apply失败或 IMU失败时 ForceSafe 锁存；正常 KEY1 Disable。
4. 单纯GPIO按键轮询/去抖（在 ControlTask 内）；不用外部中断、新任务、队列、DMA/UART；禁止复用 PC6/PC7 与 IMU SPI 引脚。
5. 新增编译期 Stage0/1/2 与显式许可宏；P1a/P1b输出测试绝不能与 P2b 编译输出路径共存；Stage0 编译链接可证明不启动 PWM。
6. 更新 Keil 工程最小新增文件；不修改原始 Actuator 与中断/时钟；输出 MAP/HEX/hash、编译开关、真实 HOST 结果及硬件使用指南。

## 构建与移交顺序

- 首先 Stage0(default) Build PASS：应不上电产生输出；再无舵机 Stage1 Build（需编译门禁有效）与 HOST PASS；Stage2 只准备文档/条件，不直接交付可自动带载活动的镜像。
- 主机测试建议合并为一个最小回归程序和若干 Case，不扩成 40 个冗余试验；必须覆盖TEST不回归、键未按不使能、稳定KEY0授权、40ms首帧、过期/故障关断、KEY1 terminal、无法重放。
- 输出 `P2b_HANDOFF.md` 填写真实 Build/HOST/LA/MANUAL，每个项目无真实执行则 NOT_RUN；列出实际修改文件和与 P1 的受影响回归。
- 任何需要改变 **Actuator故障锁存、全局控制所有权、会话恢复规则** 的需求必须停下提交总控，不得私改。
