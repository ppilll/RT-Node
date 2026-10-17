# RT-Node F103 / P2b Codex 文档包 V1.0

- 编制日期：2026-10-17；语言：中文；阶段：P2b 本地姿态跟随。
- 审查仓库：<https://github.com/ppilll/RT-Node>；编制时 main HEAD：`d11fb8b1de2da1a6c18e55970176979b42b36ce7`（P2A）。**开工前必须重新核实 HEAD、工作树和 AGENTS.md。**
- 本包最初是设计/执行说明；本轮已完成最小源码、Stage0/Stage1真实ARMCC5 Rebuild及生产C HOST验证，详见 [P2b_HANDOFF.md](P2b_HANDOFF.md)。硬件仍NOT_RUN、实物KEY未核实、G2未批准，状态PARTIAL；不得将模板或HOST PASS当现场实测。

## 先看这个状态说明

用户提供截图中可读到约 9.8015 ms（102.03 Hz）事务重复间隔、约 238.9 µs 片选窗口，能支持 H02 的 **采集时序部分证据**；截图没有可验证的 `WHO_AM_I=0x12` 字节解码、14 字节完整内容、传感器缺失处理和 PC6/PC7 无 PWM 证据。P2a 的历史 `P2a_HANDOFF.md` 仍标注 H01~H04 为 NOT_RUN；除非现场证据补齐，不得改写成 P2a ACCEPTED。

**允许 P2b 设计/编码/主机测试；不允许跳过 P2a 缺失证据直接接真实舵机带载。** 软件无负载阶段也需遵循明确编译授权及人工启动。

## 文档阅读顺序

1. `ENTRY_GATE.md`：进入 P2b、无负载、带载的分级门禁与现有证据。
2. `requirements.md`：实现范围、保持不变的 P1 约束、精简原则。
3. `design.md`：控制状态机、按钮授权、映射、限幅、失效。
4. `interfaces.md`：源码级契约和最小控制会话扩展。
5. `implementation_plan.md`：实施次序/代码范围/可选测试。
6. `test_plan.md`：紧凑的主机/编译/逻辑分析仪验收。
7. `HARDWARE_TEST_GUIDE.md`：用户 CH340 + 逻辑分析仪上板步骤。
8. `SOURCE_REVIEW.md`：当前仓库已读源码事实、资料。
9. `codex_tasks.md`：可直接复制给 Codex 的启动提示词。
10. `P2b_HANDOFF_TEMPLATE.md`：实施者回填交接模板。

## 快速决策

**优先建议**：精英 V2 KEY0/PE4 按下后手动使能，KEY1/PE3 按下停止；必须确认实际板卡按键电平及占用。其控制模式保留 P1 “一次授权、终止不可恢复”的语义；不是新增持久运行授权协议。没有经过源码/硬件核实的按键映射不得视为已经批准的硬件事实。

**禁止**：重写 Actuator、修改其故障锁存；新增独立 Actuator 写者；在 ImuTask 中执行 PWM；上电自动 Enable；P2b 启用 P1a/P1b 测试源；提前进入 P3；默认存在 SWD/UART 日志。
