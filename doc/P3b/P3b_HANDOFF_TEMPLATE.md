# P3b_HANDOFF — 待Codex实施后填写

日期：NOT_SET；仓库HEAD：NOT_SET；Codex执行环境：NOT_SET。

## 阶段状态

- P3a双端CAN通信：**USER_REPORTED_PASS**（用户报告；未在本包中见原始candump）；P3a旧交接文档可能仍标NOT_TESTED，须注明时间差异。
- P3b代码实现：NOT_IMPLEMENTED（填写真实变更后再改）
- Keil5/ARMCC5构建：NOT_RUN
- T2 Stage1 无负载 PWM：NOT_TESTED
- T3 LOCAL/REMOTE 仲裁：NOT_TESTED
- T4 STOP + TTL + 重授权：NOT_TESTED
- Stage2 受限带载：NOT_AUTHORIZED / NOT_TESTED
- 建议P3b ACCEPTED：NO（未满足完整验收条件）

## 主要修改

| 文件 | 真实改动及理由 |
|---|---|
| App/can/* | 待填 |
| App/control/* | 待填 |
| Projects/RTnode.uvprojx | 待填 |
| 其他（如有） | 待填，说明为何必需 |

## 最终协议（不变更P3a冻结值）

0x180 REMOTE_CONTROL、0x181 MODE_REQUEST、0x182 GLOBAL_STOP、0x183 HEARTBEAT、0x280 STATUS；标准数据帧DLC8，LE；TTL100ms；heartbeat100ms/timeout500ms；STATUS100ms+事件；epoch16/seq16/action16。若实现有偏离，列出具体问题并禁止宣称冻结兼容。

## 关键安全语义验收

- 正常Disarm后可再次授权：NOT_TESTED
- Fatal/Actuator latch不会被Normal STOP清除：NOT_TESTED
- GLOBAL_STOP同时覆盖LOCAL和REMOTE：NOT_TESTED
- 新epoch、旧命令不重放：NOT_TESTED
- 避免ControlSession_Accept重复generation误fatal：NOT_TESTED
- LOCAL不依赖CAN心跳：NOT_TESTED
- REMOTE TTL100ms及限速±2µs/20ms：NOT_TESTED
- 禁止CanTask/ISR直接操作Actuator：NOT_REVIEWED

## 真实构建与实机证据

- Keil命令/目标/版本、退出码、errors/warnings、HEX路径：待填，未运行不得宣称PASS。
- T2：操作/结果/一份LA截图链接：待填。
- T3：操作/结果/candump或LA证据：待填。
- T4：操作/结果/STATUS与输出证据：待填。
- 受限带载：用户单独批准时间/机械限位/物理断电手段/结果：待填。
- 未直接测得的WCET/ISR延迟不作性能通过声称。

## 遗留与下一步

真实未完成项：待填。
冻结接口是否遵守：待审。
是否建议进入P4：**NO，除非P3a/P3b全套验收完成且用户明确批准。**
