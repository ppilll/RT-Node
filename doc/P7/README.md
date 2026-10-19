# RT-Node F103 · P7 Codex 实施包 V1.0

**生成日期：** 2026-10-19  
**GitHub：** https://github.com/ppilll/RT-Node  
**审查基线：** `main` / `c3e9408013ae009e2b6cb49ac8d53f79d169e430`（P6；2026-10-19再次核对为最新提交）  
**性质：** 批准规范与Stage0实现包。实现/主机验证/正式构建已经完成；硬件数据仍待用户测试。

## 本轮实际状态（2026-10-19）

- **PASS（主机）**：40项原生编译/运行/负向守卫检查，13项Python离线测试；不等于硬件通过。
- **PASS（构建）**：Keil5/ARMCC5正式Stage0 Rebuild，0 errors/0 warnings；Flash 46012B、静态RAM 16760B，HEX未进入安全页。
- **NOT_RUN（实机）**：G-B/T1/T2与真实栈/复位/IMU输入；G-C/Stage2未授权。真实数据保持待测。
- 当前实现新增 `PERF? / PERF2? / PERF3? / PERF4? / CANC?`；最大回复56/57/57/57/57B（CRLF计入）。
- [P07_HANDOFF.md](P07_HANDOFF.md) 是实际变更、构建证据、命令行及人工停点的交接；[operator_runbook.md](operator_runbook.md) 为现场顺序。
- 本机没有pyserial；`--help`和离线测试可无依赖运行；真实串口前由用户安装 `scripts/requirements.txt`。

## 如何使用

1. 将此包解压到 Codex App 可读取的位置。使 Codex App 打开现有 `ppilll/RT-Node` 的**代码检出目录**，并将本包路径提供给 Codex。
2. 复制 `00_CODEX_START.md` 的**全文**到 Codex App，附上这个包或提供解压目录位置。
3. Codex 按 `codex_tasks.md` 审阅最新真实代码、实现最小观测、完成可运行的 PC/RK 脚本、执行它有条件完成的主机回归/构建。
4. Codex **不得自动烧录、接通舵机、执行实机测试、改动Flash末页或清除P6授权锁**。没有 Keil/板卡时应如实标记 `NOT_RUN`。
5. Codex 完成后把 `doc/P7/P07_HANDOFF.md` 和改动清单/构建日志发回当前对话；由用户按 `operator_runbook.md` 实机操作，我负责审核真实结果与整理简历证据。

## 内容

| 文件 | 用途 |
|---|---|
| `00_CODEX_START.md` | 可以直接复制给 Codex 的执行指令 |
| `source_audit.md` | 已核查源码与必须再次核对的接口/安全门禁 |
| `measurement_plan.md` | 统计字段、计量口径、观测窗口、报文格式 |
| `implementation_plan.md` | 允许改动的文件与最小架构、主机脚本规格 |
| `codex_tasks.md` | C1～C5 的任务、顺序与验收标准 |
| `test_plan.md` | G-A/G-B/G-C、T1、T2、R、M 测试矩阵 |
| `operator_runbook.md` | Codex 完成后由人执行的逐步操作和停止条件 |
| `templates/` | 真实结果、简历证据、交接文档空白模板 |
| `reference/P7_APPROVED_TEST_PLAN_V1.0.md` | 已批准的完整测试方案原文 |

## 立即批准与未批准的事项

**已批准：** 实现测量插桩、测试脚本、纯C主机测试、正式**Stage0**固件编译/回归；之后在实际操作者手中完成 Stage0 IMU、2分钟基准、10分钟并发和通信测试。参数：开机10秒预热，CAN 50帧/s，UART 10 PING/s，低频诊断5秒。

**本次不自动授权：** Stage1通电输出验证（即使舵机信号/电源断开也需再次确认具体条件）；Stage2任何带载动作；P6 IWDG重新故障注入；Flash安全页擦写/解锁；重写CAN线上协议；修改控制门限或安全策略。

**合格数据原则：** 源码事实、构建测试、纯C主机测试、实机观测必须分开；新性能百分比必须有可比的前后数据；没有硬件证据不能把算法验证写成机械云台轨迹精度验证。
