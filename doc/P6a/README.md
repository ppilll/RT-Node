# P6a：健康状态整合、跨复位锁定与本地人工恢复

**先于 P6b 执行**。P6a 不要求实际打开 IWDG、不做真实故障注入、不更改 CAN 线上协议。

## 输入

- 根目录 `DECISIONS_AND_LIMITS.md`（已批准策略 + 风险）
- `references/source_audit.md`（P5 已有能力）
- 本目录 `design.md`、`interfaces.md`、`implementation_plan.md`、`test_plan.md`、`codex_tasks.md`

## 输出

- P6a 代码变更（建议 `App/health/health_app.*`、`main.c`、`control.c`、`uart_app.c`、Keil 工程）。
- 经过代码审查的 Flash 最后页预留及无覆盖证明（应用实际链接范围与 HEX 最高地址即可；无需重复做 HASH）。
- 编译状态（用户提供 Keil 真实结果）、最小 T1a 验证结果。
- `P6a_HANDOFF.md`（用本目录模板填写）。

## 固定验收

- 初始空白 Flash 页时 **LOCKED**，不自动 LOCAL。
- 经手动首次初始化可记录 `AUTO`；普通冷启动仍保留 P5 一次自动 LOCAL 机会（仅按 P5 原有安全条件）。
- 记录 `LOCK` 后，无论 NRST/软件复位/POR/PDR 均锁定；CAN MODE/CONTROL 不能绕过。
- `SAFE CLEAR` 恢复 `LOCK -> MANUAL`：从此所有重启都不再自动 LOCAL，必须新的有效 MODE 授权。
- KEY0/KEY1/CAN 协议及旧安全路径不变。
- 当前 P6a 还未加入看门狗；模拟锁/重启测试需明确标注“接口逻辑验证”，不能称为实际 IWDG 验证。
