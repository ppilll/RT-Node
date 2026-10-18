# T2/T3 — OLED + 按键 + 联合运行核心测试

## T2：四页 OLED

1. 开机：I²C1 应能在地址 0x3C ACK（若地址不同记录真实值），初始化 128×32 显示，ENV 页数据来自 P5a 快照；板载 EEPROM 0x50 不访问。
2. KEY1：依次 ENV→IMU/PWM→CAN→UART→ENV；单击仅一页、长按不连翻、松开后能再次触发。
3. ENV：上屏温湿压与 `ENV?` 相近，已失效状态显示 `--/ERR/STALE`。
4. IMU/PWM：真实 Roll/Pitch 与 `Imu_GetSnapshot` 一致；Pan/Tilt 是 `Actuator_GetSnapshot` 的**已提交 PWM 目标**，未使能时不伪称运行。
5. CAN：RK3588 SocketCAN 发少量实际命令，屏上最新 ID/请求目标或模式变化与真接收事件一致；不为了显示改变协议。
6. UART：PC 发 `PING`/`COUNT?`/`ADC?`/`ENV?` 少量命令，屏上能显示实际完整解析摘要/计数/ADC；旧 UART 回复不受影响。

## T3：简短联合运行

同时运行 ControlTask 20ms、ImuTask 10ms、CanTask、UartTask、ADC DMA、BME280 500ms 和 OLED 200ms 初始目标。人工观察短时连续显示、姿态/通信/ADC 更新，控制无明显停顿或不正常舵机输出；OLED 更新较慢时允许调低刷新率，并记录实际配置。不做 P7 压测。

**KEY0 / 自动启动是 T3 必须补充的简短安全观察：**

- 上电按住 KEY0 → 本次开机无 PWM 输出；松手仍锁定。
- 正常上电 IMU READY 后只自动启动一次；保持原 PRIMING 和 LOCAL 参考角行为。
- 正常运行按 KEY0 → 输出停止、屏幕可继续显示；松开 KEY0 不恢复。
- KEY0 锁定后发送合法 CAN MODE / CONTROL 命令 → 不恢复 PWM。
- MCU 重启 → 再次满足 IMU 等条件才允许重新自动启动。
- CAN STOP 不得触发自动 LOCAL 重启；未锁 KEY0 时有效 CAN MODE 仍遵守原协议。

**安全提示：** 默认 `P3B_OUTPUT_STAGE=0` 不使能 PWM；任何带舵机负载的观察须在用户已批准的既有 Stage2 门控/安全环境下进行。仅对已授权的安全硬件配置执行相应步骤。没有在线调试器，不要求 Watch/示波器/万用表。

**最小记录：** 测试日期、固件 commit、每项操作、实际结果、PASS/FAIL、必要串口输出或一张 OLED 照片。缺数据时为 NOT RUN，不伪造截图或延迟/精度数值。

## 本轮固件与执行边界（2026-10-18）

用户暂不进行T2/T3，所有实机项NOT RUN。代码已完成，Keil Stage0镜像为`Projects/RTnode.hex`；版本`main @4093ca3 + 未提交P5a/P5b修改`。实际配置：500ms构图间隔、16B数据块、5ms步进、I²C 100kHz。

Stage0可做T2及KEY0软件锁存观察，不能以“没有PWM”证明自动授权/priming/运行中停机。IMU/PWM页显示`PWM:OFF STAGE:0`；KEY0按住启动或短按后显示`IMU/PWM KEY0 LOCK`，松开/后续CAN请求不得清除。真实输出T3-B/C/E仅在用户已有明确输出许可及安全负载条件下执行；本轮不修改Stage/Stage2许可。

T2使用CH340 115200 8N1发送`PING\n`、`COUNT?\n`、`ADC?\n`、`ENV?\n`检查旧回复及UART页。CAN页REQ为原始接收请求，标OLD仍为历史请求，不代表执行值。各页每行最多21字符，无效样本占位。

T3串行等待0x280反馈再构造下一动作：STATUS字节4/5为当前epoch小端；MODE 0x181=`01 mode action_lo action_hi epoch_lo epoch_hi 00 00`；CONTROL 0x180=`epoch_lo epoch_hi seq_lo seq_hi pan_lo pan_hi tilt_lo tilt_hi`；STOP 0x182=`01 01 action_lo action_hi 00 00 00 00`。使用新action/seq、MODE反馈后的新epoch，接收时间严格晚于模式边界，REMOTE控制间隔<100ms。锁定后这些有效帧也不得恢复；未锁时普通STOP后等待数周期不应自行恢复LOCAL，再发有效MODE验证协议恢复。不要用旧epoch或过期CONTROL充当锁定证据。

主机回归可运行`doc/P5b/tests/run_host_tests.ps1`（本机MSVC路径见脚本），只用模拟HAL/RTOS执行真实状态机和显示辅助函数，不可回填T2/T3 PASS。不复测P1–P4全套。
