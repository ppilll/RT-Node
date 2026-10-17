# P3a 需求与边界（V1.0）

## 要交付

- STM32F103ZET6 CAN1（bxCAN）、默认 PA11 RX/PA12 TX、Classical CAN/11-bit Standard ID。
- 复用仓库中已有 STM32F1 HAL CAN 实现，不新写寄存器级 CAN 驱动。
- FIFO0 过滤器、RX 中断、静态接收缓冲区、CanTask、非阻塞 STATUS 发送。
- 根据 `interfaces.md` 校验五种帧；P3a 接收四类上行帧，回传 STATUS `0x280`。
- RK3588 端使用 SocketCAN 的 `ip/candump/cansend` 验证双向链路。
- 不要声称尚未实测的构建、总线或 PWM 结果。

## P3a 明确不做

- **不接受 REMOTE 模式进入、不调用 Actuator_Enable/Apply，不修改 ControlTask 以接入 CAN。**
- 不允许 CAN 自动/间接开启 TIM3 PWM。保留 P2b 默认 `P2B_OUTPUT_STAGE=0`，P1a/P1b 输出测试关闭。
- 不实现远程 Pan/Tilt 输出、ControlSession 运行时再授权、LOCAL/REMOTE 仲裁、命令超时执行器联动或远程限速；这些全部属于 P3b。
- 不加 CAN FD、CRC8/CRC16 应用层校验、分包、加密、ROS/MQTT 或动态分配。
- 不重复 P1/P2 的大量旧测试、全套哈希校验或证据目录。

## 必须保留

- ControlTask 20ms 单一正常执行器写者；既有 DWT 调度/期限检查及致命故障锁存。
- IMU ICM20602 + SPI1、`ImuTask` 10ms、现有 `LocalFollow` 接口、TIM3 PC6/PC7 和 Actuator 安全限制。
- 静态 FreeRTOS 分配（`configSUPPORT_STATIC_ALLOCATION=1`、动态=0）；RTOS 1kHz/32bit tick。
- CAN 中断不能长时间执行协议业务；中断和 CanTask 不调用 Actuator。

## 验收约束

P3a ACCEPTED 需要：CAN 构建、双向实机收发、ID/DLC/Payload 验证、STATUS 正常、CAN 不意外使能 PWM。无真实 RK3588/STM32 实测时，最多报告 CODE_COMPLETE / BUILD_PASS，不得填 ACCEPTED。
