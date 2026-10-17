# P2b_HANDOFF — Codex/操作者回填模板

状态：`IMPLEMENTED / HARDWARE NOT_RUN`（模板默认，不等于事实）

## 1. 仓库与镜像身份

- 进入 HEAD / 分支 / 工作树：
- 实际提交SHA与是否已推送：
- 工程 `Projects/RTnode.uvprojx`、编译器版本 / 编译宏：
- Stage0/Stage1/Stage2 实际执行情况：
- SHA256(HEX) 与固件路径：

## 2. 源码变化与不变量

- 修改/新增文件列表：
- protected `Actuator/interrupts/startup/FreeRTOS port/IMU` 改动审计：
- LOCAL_IMU来源/会话授权/终止规则：
- KEY GPIO核对结果：
- 绝对姿态映射参数及方向：
- IMU stale/failure 与 ControlTask Deadline/Driver fail 关断行为：

## 3. 实际证据

| ID | BUILD/HOST/LA/MANUAL | PASS/FAIL/NOT_RUN | 原始证据路径 |
|---|---|---|---|
| S01 | STATIC | NOT_RUN | |
| B01 | BUILD | NOT_RUN | |
| U01 | HOST | NOT_RUN | |
| U02 | HOST | NOT_RUN | |
| U03 | HOST | NOT_RUN | |
| H01 | LA无负载 | NOT_RUN | |
| H02 | LA无负载 | NOT_RUN | |
| H03 | LA无负载 | NOT_RUN | |
| H04 | MANUAL带载 | NOT_RUN | |

- P2a H01~H04 是否有新现场补充证明（逐项）：
- LA采样率/阈值/软件与截图/raw/CSV：
- 控制周期精确WCET/运行栈余量：`NOT_MEASURED`，除非有独立真实读出。
- 故障与回归：

## 4. 资源与结论

- ROM Code/RO/RW/ZI；RAM/栈静态预算：
- 已知限制、残留问题：
- G0/G1/G2 状态：
- P2b ACCEPTED? YES/NO（逐条证据支持）：
- 是否允许 P3? 默认 NO。
