# 外部依据（官方优先）

1. RT-Node GitHub 源码：https://github.com/ppilll/RT-Node
2. P5 commit：https://github.com/ppilll/RT-Node/commit/fc427fa7c0c3a0a3e81b17e75096e1bc00c5f6bf
3. ST RM0008（RCC_CSR 重启原因、IWDG、BKP 备份域）：https://www.st.com/resource/en/reference_manual/cd00171190-stm32f101-103-105-107-stm32f100-series-armbased-32bit-mcus-stmicroelectronics.pdf
4. ST PM0075（Flash 半字程序写入/页擦除/保护）：https://www.st.com.cn/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf
5. ST STM32F103ZE 产品页面（512KiB、高密度、VBAT）：https://www.st.com/en/microcontrollers-microprocessors/stm32f103ze.html
6. ST STM32F103xC/D/E datasheet（LSI 30–60kHz，典型 40kHz）：https://www.st.com/resource/en/datasheet/stm32f103ze.pdf
7. FreeRTOS xTaskDelayUntil 文档：https://freertos.org/Documentation/02-Kernel/04-API-references/02-Task-control/03-xTaskDelayUntil
8. FreeRTOS 官方主页：https://www.freertos.org/

文档中的秒数都是根据典型/规格 LSI 推导的**计算值**，未代表真实板子的示波器或逻辑分析仪测量。源码中的 HAL 和 FreeRTOS API/宏优先以仓库随附版本为准。
