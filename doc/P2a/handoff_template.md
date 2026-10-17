# P2a_HANDOFF.md（交接模板，待真实运行回填）

> 本文件是模板。未取得的证据填写 NOT_RUN / NOT_MEASURED。不能因为设计文档存在就记录为已实施。

## 1. 仓库/固件身份

- 日期、执行者：`TBD`
- 进入时 HEAD / 交接时 HEAD：`TBD / TBD`
- Git status / 未提交工作处理：`TBD`
- MDK 工程：`Projects/RTnode.uvprojx`
- ARMCC 版本：`TBD`
- HEX 文件 SHA-256：`NOT_RUN`
- 厂商包 Resources 实际路径 / SHA-256：`TBD / NOT_IN_REPO`（设计包供参考值 `39d64156...ba791`，必须实测一致）

## 2. 变更内容

- 新增文件：`TBD`
- 修改文件：`TBD`
- P1 禁改文件 diff 对比：`NOT_RUN`
- 新增外设与管脚：SPI1 PA5/6/7；PC4 CS；VCC=3.3 V；其他 `TBD`

## 3. 实际技术实现

- WHO_AM_I、SPI Mode、时钟及片选：`TBD`
- 寄存器配置与读回：`TBD`
- DMA/FIFO/IRQ：应为 OFF；实测/源码检查 `TBD`
- ImuTask 周期/优先级/栈静态配置：`TBD`
- 量程系数/校准/融合参数/快照有效性：`TBD`
- 初始化与 SPI 错误处理：`TBD`
- P2a 禁用执行器证据：`NOT_RUN`

## 4. 资源与构建

| 项目 | 结果 | 证据 |
|---|---|---|
| ARMCC Rebuild/Link | NOT_RUN | TBD |
| Flash（ROM bytes） | NOT_MEASURED | MAP TBD |
| SRAM（RW+ZI bytes） | NOT_MEASURED | MAP TBD |
| ImuTask 栈静态预留 | TBD | MAP/源码 TBD |
| ImuTask 栈高水位 | NOT_MEASURED | 无运行时读出则维持此状态 |
| ControlTask 周期/WCET | NOT_MEASURED | 无外部 trace 则维持此状态 |

## 5. 测试结果与证据

| Test ID | 状态 | 证据路径 | 备注 |
|---|---|---|---|
| B01 | NOT_RUN | — | 构建 |
| S01 | NOT_RUN | — | P1 文件静态回归 |
| U01 | NOT_RUN | — | 数据解码/换算 |
| U02 | NOT_RUN | — | Roll/Pitch |
| U03 | NOT_RUN | — | 零偏/dt |
| U04 | NOT_RUN | — | invalid/有界失败 |
| H01 | NOT_RUN | — | WHO_AM_I SPI LA |
| H02 | NOT_RUN | — | burst采集 |
| H03 | NOT_RUN | — | PC6/PC7 无 PWM |
| H04 | NOT_RUN | — | 上电设备缺失 |

## 6. 遗留及交接

- 已知限制/未实现内容：`TBD`
- 厂商源许可证问题（是否复制厂商代码）：`TBD`
- P1 历史文档技术债务：保留，不自动补虚构证据。
- 需要用户下一步执行：按 `hardware_guide.md`、`test_plan.md` 完成实机；上传原始 LA 文件。
- P2a 状态：`NOT_RUN / IMPLEMENTED_PARTIAL / ACCEPTED（须有对应真实证据）`
- 是否允许进入 P2b：默认 `NO`，P2a Gate 完成并获得用户/总控批准后单独决定。
