# P07_HANDOFF · Codex开发交接（模板）

## 一、代码与工作区

- 本次基线 HEAD：待填
- 工作分支/未提交用户改动及保留方式：待填
- 已修改/新增文件逐项列表：待填
- 当前HEAD/工作区状态：待填
- 是否推送/提交远端：**未获单独授权时不得推送**

## 二、核心实现

- Control DWT采样点/10s开窗/周期min-max/max elapsed/terminal后计数方法：待填
- UART `PERF?`/`PERF2?`/`CANC?` 真实回复格式和最大长度：待填
- CAN事件累计如何与已有安全overrun标志隔离：待填
- PC/RK脚本CLI参数、依赖、日志/JSON输出说明：待填
- 如何避免旧KEY0/KEY1危险指导：待填

## 三、实际执行的验证

| 项目 | 实际命令 | PASS/FAIL/NOT_RUN | 证据路径与异常 |
|---|---|---|---|
| C1 G-A主机算法测试 | 待填 | NOT_RUN | 待填 |
| C2 周期/回绕/terminal主机单测 | 待填 | NOT_RUN | 待填 |
| C3 UART最大长度/命令回归 | 待填 | NOT_RUN | 待填 |
| C4 Python脚本离线Parser/pack测试 | 待填 | NOT_RUN | 待填 |
| C5 已有P2b/P3b/P6回归 | 待填 | NOT_RUN | 待填 |
| 正式 Keil5/ARMCC5 Stage0 Rebuild | 待填 | NOT_RUN | 待填 |
| Stage0 HEX/链接MAP地址未占安全页 | 待填 | NOT_RUN | 待填 |
| G-B/T1/T2/R真实硬件 | **用户后续执行** | NOT_RUN | 不得造数据 |
| G-C Stage1 LA | **独立授权后由用户执行** | NOT_RUN | 不得擅自执行 |
| Stage2任何带载动作 | **未批准** | NOT_RUN | 不得执行 |

## 四、正式Stage0产物

- `P3B_OUTPUT_STAGE=0`, `P6_WDG_TEST_ENABLE=0`及历史测试宏全部确认：待填
- ARMCC具体版本/Keil重建日志：待填
- Code/RO/RW/ZI, Flash=Code+RO+RW, RAM=RW+ZI：待填（无Keil则NOT_RUN）
- HEX/MAP实际文件路径：待填
- Flash安全页`0x0807F800`保护结论与依据：待填

## 五、后续用户命令（从这里移交）

PC（真实COM端口按现场调整）：待填明确命令  
RK3588（实际can0）：待填明确命令  
预热与A/B计数窗口确认方法：待填  
异常停止规则：待填

## 六、残留问题 / 风险 / 尚需人工确认

- 待填：无Keil/硬件、不可同步的两端测试起止、CAN队列统计范围、UART自动ADC行、P6安全页下载保护、P6历史实测证据是否可追溯等。

**最终状态摘要：** 代码是否实现/主机是否PASS/正式Stage0是否真构建/实机尚未运行；只把真正跑过的项记PASS。
