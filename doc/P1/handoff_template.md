# P01a_HANDOFF.md 模板

> 此文件仅是模板；Codex 或阶段负责人必须填实际结果。禁止预填 PASS、实际时间数据、测量频率或实机成功声明。

## 0. 阶段元数据

- 阶段：P1a 工程基线与执行器安全迁移
- 完成日期：`TBD`
- 版本号：`TBD`
- 验收建议：`PARTIAL / BLOCKED / FAILED / ACCEPTED（只选一个，基于证据）`
- 新仓库基线 SHA：`TBD`
- 实施完 HEAD SHA：`TBD`
- 旧仓库参考 SHA：`92d8f45bc9c7df55f6e2abea4b4abfeb090a3e3d`（只读）

## 1. 实际修改范围

| 文件 | 变更性质 | 理由 | 负责人/对话 | 对应 diff/commit |
|---|---|---|---|---|
| `TBD` | `TBD` | `TBD` | `C1/C2` | `TBD` |

说明是否更改了原始 `.uvprojx`、本地调试选项、HAL 配置、Startup、AFIO 初始化和测试入口；明确未进入 P1b。

## 2. 旧代码复用溯源

| 旧文件与 commit | 新文件 | 保留 API / 修改差异 | 回归测试 |
|---|---|---|---|
| `Visionarm_MCU/App/actuator/... @ 92d8f45...` | `TBD` | `TBD` | `TBD` |

## 3. 构建证据

- Keil project 位置、Target 名、ARMCC 版本、宏和优化：`TBD`
- 实际构建命令：`TBD`
- Build Log 文件/摘要：`TBD`
- Build Verdict：`PASS / FAIL / NOT_RUN`
- AXF、HEX、MAP 位置：`TBD`
- Flash ROM / RAM 静态链接占用：`TBD`（若无 MAP，不填估计值）
- 已知警告和未解析依赖：`TBD`

## 4. 静态检查和功能证据

| Test ID | 实际执行环境 | 结果 PASS/FAIL/NOT_RUN | 文件/证据 | 备注 |
|---|---|---|---|---|
| S01–S07 | `TBD` | `TBD` | `TBD` | `TBD` |
| H01–H10 | `TBD` | `TBD` | `TBD` | `TBD` |

### 波形统计（只有真实分析仪记录才能填）

- 设备、采样率、门限、通道映射：`TBD`
- 频率/周期统计（N、min/avg/max）：`TBD`
- Pan 不同 CCR 实测（N、min/avg/max）：`TBD`
- Tilt 不同 CCR 实测（N、min/avg/max）：`TBD`
- 启动/禁用/故障/恢复瞬态：`TBD`
- 原始波形文件与截图：`TBD`

## 5. 安全行为与故障

- 上电至软件初始化期间的已知局限：`TBD`
- 未使能/初始化失败关断证据：`TBD`
- 故障锁存 + 拒绝重使能证据：`TBD`
- 重新授权与防旧命令重放证据：`TBD`
- 受限机械测试准备/动作结果：`TBD`
- 已知故障、残余风险与硬件限制：`TBD`

## 6. 设计偏差/审批记录

- 相对于要求或初始阈值的变更：`无 / 明细及原因`
- 变更批准者/批准日期：`TBD`
- 对后续阶段的影响：`TBD`

## 7. Gate 和下一步

- P1a Gate 判定与证据：`TBD`
- 仍缺少的测试与责任人：`TBD`
- P1b 允许启动？`仅当 P1a 全部必要 Gate 满足且获得阶段批准时为是`
- 可交付给总控的结论：`TBD`
