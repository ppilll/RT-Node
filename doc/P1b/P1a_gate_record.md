# P1a Gate 记录（C0 需完成）

## 已知用户报告（2026-10-16）

- 用户使用无 SWD 自动测试 Case1–3，并明确确认逻辑分析仪波形与预期占空比/脉宽变化吻合。
- 记录为：`Case1 OPERATOR_REPORTED_PASS` / `Case2 OPERATOR_REPORTED_PASS` / `Case3 OPERATOR_REPORTED_PASS`。未提供 CSV/最小/均值/最大脉宽数字；不能假设这些数据已保存。
- 用户此前说明舵机独立供电、共地，使用旧工程的执行器参数与限幅。但未在本轮明确报告 H10 受限机械运动完成。
- 最新远端 `main` 仍 `7588842c`，未见 Case1–3 自动测试增量；本地实际源码应由 Codex C0 记录。

## C0 填写项

```text
用户本地源码 HEAD / commit / diff：
Case1–3 编译固件 hash / Keil 编译是否成功：
Case1 基础中位 + Pan/Tilt 目标变化（用户报告）：OPERATOR_REPORTED_PASS
Case2 超界限幅 + 同步（用户报告）：OPERATOR_REPORTED_PASS
Case3 禁止 / 恢复 / 故障（用户报告）：OPERATOR_REPORTED_PASS
复位首次输出安全 / 默认不使能记录：
H10 机械中位和受限小幅实际运动：PASS / FAIL / NOT_RUN
实际舵机/电源/机械间隙：
MAP / Flash/RAM：
P1a Gate：PARTIAL / ACCEPTED / BLOCKED / FAILED
审批者/日期/证据清单：
```

## H10 快速补齐（用户实机操作，不需重做已通过的波形）

确保无负载 Case 已通过且可安全禁用，先切断舵机电源，确认地线共地、供电规格、电平和无遮挡机械行程；接入信号，重新供电；**不要把 Case2 超界或 Case3 故障注入自动脚本连接到舵机**。用预先批准、受限的仅中位和小幅动作测试镜像：1500/1500 中位→Pan 1450/1550→Tilt 1450/1550，逐轴观察方向、卡死、异响、供电复位及机械限位。记录用户实际结果/视频或动作说明。若当前三个 Case 固件均会在带载状态自动执行全范围切换，需让 Codex 单独提供 H10 有界、小幅、单次授权镜像，默认仍关闭，禁止复用激进无负载脚本。完成后恢复正式禁用固件。

H10 通过且 S03 真实构建记录齐全后，才提出 P1a ACCEPTED；否则本地可以准备 P1b 文档但不能正式启动带载 P1b。

## 2026-10-17 本地核对与本轮授权

- 真实本地 HEAD：`7588842c29bfc0d1f5fce3011c727b4df38fc3d5`。本轮没有联网刷新远端，不能把历史远端记录当成本轮查询。
- 用户本轮明确要求执行 C1；据此仅推进不带载的 C1 源码准备、审查和构建，不将此授权等同于 H10 已通过或正式 P1a Gate 达标。
- Case1–3 保持 `OPERATOR_REPORTED_PASS`；没有要求重做，没有新增/编造波形数值。真实本地自动测试源码存在于未提交工作树，进入本轮时的状态、diff、源码哈希保存在 `evidence/C1/entry/`。
- 进入本轮时 `main.c`、`interrupts.c`、`FreeRTOSConfig.h`、uvprojx 已有 C1 改动，`rtos_hooks.c` 尚未跟踪，已有构建产物也是 C1 调度镜像；不能把这些产物哈希作为历史 Case1–3 固件哈希。历史 Case 镜像哈希与完整编译证据仍未确定。
- 本轮 C1 实际 ARMCC5 Rebuild 成功，证据见 `C1_handoff.md`；它不补代 P1a 当时的编译/机械记录。
- H10：`NOT_RUN / 未获完成报告`；实际机械动作、电源与间隙记录未补齐。
- **P1a Gate 仍为 PARTIAL；本轮 C1 仅源码/构建完成，禁止据此开始带载测试或自动执行 C2。**
