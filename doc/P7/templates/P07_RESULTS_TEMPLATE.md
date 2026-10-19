# P07_RESULTS · 实测结果（空模板，不得预填PASS）

测试日期：待填  
测试负责人：待填  
代码提交：待填  
镜像构建宏：待填（确认Stage0/P6生产）  
Keil构建：`NOT_RUN` / `PASS` / `FAIL`  
P6策略状态（HEALTH? P/A）：待填  
说明：任何空格表示尚无实测，**不是零异常**。

## G 手势/IMU

| ID | 手段 | 实际观察/数据 | 证据路径 | 状态 |
|---|---|---|---|---|
| G-A | 主机LocalFollow/ImuFusion测试 | 待填 | 待填 | NOT_RUN |
| G-B | Stage0板卡IMU OLED静止/倾斜 | 待填 | 待填 | NOT_RUN |
| G-C | Stage1无负载PC6/PC7 LA（独立审批） | 未批准前不执行 | 待填 | NOT_RUN |
| G-D | Stage2带载机械云台（未批准） | 未批准 | — | NOT_RUN |

## T1 实时性

| 指标 | T1-A 120s | T1-B 600s | 证据字段 |
|---|---|---|---|
| 固件/Stage/预热起点 | 待填 | 待填 | build / PERF2 |
| 实际持续时间(s) | 待填 | 待填 | Host monotonic |
| 采样数 | 待填 | 待填 | `P7N n` |
| 入口周期min(us) | 待填 | 待填 | `P7 a / 72` |
| 入口周期max(us) | 待填 | 待填 | `P7 b / 72` |
| 最大绝对偏差(us) | 待填 | 待填 | calc |
| 最大观测执行段(us) | 待填 | 待填 | `P7 x /72` |
| 独立周期违例数 | 待填 | 待填 | `P7 v` |
| Control/Idle/Health栈最小剩余(words) | 待填 | 待填 | `P7N c/i/h` |
| 控制终态异常/IWDG意外复位 | 待填 | 待填 | HEALTH/STATUS/日志 |
| 测试结论 | NOT_RUN | NOT_RUN | 判据说明 |

## T2 CAN/UART

| 指标 | 实际数据 | 数据来源 | 解释限制 |
|---|---|---|---|
| CAN配置比特率 | 待核 | RK `ip link` | 配置不是吞吐 |
| CAN脚本计划/成功发送数 | 待填 | RK JSON | 成功send != MCU收到 |
| CanTask已处理帧增量 | 待填 | `CANC r` | 含最终无效帧 |
| Queue失败事件差值 | 待填 | `CANC q` | 不等于线层丢包 |
| CAN overrun/error事件差值 | 待填 | `CANC e` | 可能重复事件通知 |
| UART PING发送/PONG正确/超时 | 待填 | PC JSON | 无单包ID，按总数匹配 |
| UART RX/OVF增量 | 待填 | `COUNT?` | OVF不是丢字节数 |
| T2 UART边界/30s | 待填 | PC JSON | 看具体分段/回绕 |
| T2 CAN协议烟测/30s | 待填 | RK/STATUS | Stage0无PWM |
| 测试结论 | NOT_RUN | — | 失败照实记录 |

## R P6证据（优先复用，不主动再次注入）

| 条目 | 实际记录/文件 | 状态 |
|---|---|---|
| Health RUN / IWDG ON | 待填 | NOT_RUN |
| IWDG真实复位，R=IWDG | 待填 | USER_REPORTED |
| 重启后 L=1/P=LOCK | 待填 | USER_REPORTED |
| 旧控制授权不恢复 | 待填 | USER_REPORTED |
| NRST/POR后锁保持（如已验证） | 待填 | NOT_RUN |
| 两步SAFE恢复后 MANUAL/A=0（如已验证） | 待填 | NOT_RUN |

## M 资源

Code=待填 B；RO=待填 B；RW=待填 B；ZI=待填 B；Flash=Code+RO+RW；静态RAM=RW+ZI；构建结果/脚本=待填；Stage0安全页边界检查=待填。

## 总结

实机PASS项：待填；FAIL项：待填；未授权项：G-C/G-D；需要重测/定向优化：待填；尚不可用于简历的数值：待填。
