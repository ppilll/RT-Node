# P6a 极简验收（不冒充 IWDG 实测）

环境：Keil MDK5 + ARM Compiler 5.06u7 + CH340 串口下载，PC 串口助手、OLED、RK3588 SocketCAN，优先舵机电源断开及 `P3B_OUTPUT_STAGE=0`。若要允许实际 PWM/机械负载，先另行批准。

## A. 编译 + 存储隔离（必须）

- Keil build 无编译/链接错误；新源文件/驱动均在 `.uvprojx` 生效。
- IROM `0x08000000..0x0807F7FF`，尾页 `0x0807F800..0x0807FFFF` 独占；实际输出镜像不覆盖数据页。
- 确认 CH340 串口下载操作是否 mass erase；若会擦掉数据页，首次升级后重新本地初始化并记录该限制。

## B. 安全启动、人工初始化与恢复接口

1. 安全页为空时启动：`HEALTH?` 显示 `LOCK=1, POLICY=EMPTY`（格式以实现为准），不允许正常模式授权或意外 PWM。
2. 在 `P3B_DISABLED`、KEY0 未按、执行器禁用、无 driver fault 的条件下发 `SAFE CLEAR?` -> `SAFE READY 10S`；再发 `SAFE CLEAR YES` -> `SAFE REBOOT`。
3. 重启后记录为 `AUTO` 且恢复引导这一次不自动 LOCAL；仅后续正常冷启动按旧 P5 自动 LOCAL 机会。（当前 `P3B_OUTPUT_STAGE=0` 不输出实际 PWM。）
4. 在窗口超时或 KEY0 停止锁存、PE4 低、非禁用模式、driver fault 时恢复被拒绝。不得为清除 KEY0 而在本模块加新后门。
5. 人工发 MODE=LOCAL/REMOTE（按原协议），只有新且符合 epoch/action 的请求能进入原模式；读出 `HEALTH?` 数据不应强制恢复旧命令。
6. `LOCK -> MANUAL` 需要之后的 P6b T2 实际 IWDG 产生 LOCK 验证；本阶段可以用不启用实际 PWM 的**纯测试构建/离线函数测试**验证解析器与状态机，不得声称已经实测看门狗。

## C. 初步观测与文档

仅记录：操作、预期、实际、PASS/FAIL/NOT RUN、必要的 1～2 行串口回执；不做几十种故障注入。

## 必须标注的残余风险

- Flash 追加时异常掉电可能造成非法半字，默认锁定，需要离线维修。
- IWDG reset 后到锁记录持久写入之间的供电丢失竞态，不能由纯软件绝对消除。
- 串口人工恢复没有密码学身份认证；本地 UART 若由上位机转发，可能被软件自动执行。
