# P2a 实际源码与资料检查

日期：2026-10-17。进入时 local `main` HEAD 为 `c9d48211a11ed60f949e67da49cefff3a9d1a897`；HTTPS `git ls-remote https://github.com/ppilll/RT-Node.git refs/heads/main` 返回同一SHA。SSH查询因本机known_hosts访问失败，未修改SSH配置；HTTPS已独立核对远端。

## 仓库规则与现场

- 仓库递归及当前目录祖先未找到 AGENTS.md，没有创建规则文件，也没有并行委派。
- 进入时只有 `M temp1.log`、`?? Resources/`、`?? doc/P2a/`。保留这些内容；未 reset/rebase/stash、未提交或推送。
- temp1.log 进入/交接 SHA-256 均为 `C3AF9252D10B0CFAEC9895DC240F63E4C03D360F95EF4FBAA25D7A5CCF5CC742`。
- `doc/P2a/README.md` 指向的 `doc/P2/P2_DESIGN_BASELINE_V1.0.md` 实际不存在；实际读的是 `doc/P2a/P2_DESIGN_BASELINE_V1.0.md`，以及README列出的另外全部9份文件。没有冒称读取不存在路径。
- 已读实际 Actuator、Control/Policy/Config、main、interrupts、rtos_hooks、freertos_static、FreeRTOSConfig、RVDS port关键启动部分、HAL配置、HAL SPI阻塞传输/超时/结束路径、原uvprojx及P1b interfaces。
- `.gitignore` 首行原有 `*log\` 会令rg默认忽略规则解析报错。本次搜索使用`--no-ignore`，没有顺带修改它。

## P1 资源与安全接口

- main 先执行 P1a_ForceSafeOutput，再时钟、Actuator_InitSafe、Control_Init；新增 Imu_Init 在这些之后、SysTick handoff之前。Imu_Init只静态创建任务，失败沿用fatal。
- ControlTask：priority1，20ticks，256words，保留DWT/执行预算/晚唤醒保护及单次授权控制策略。实际默认P1a/P1b测试宏均为0；P2a编译单元会拒绝任何输出测试模式。
- Actuator 独占 TIM3全重映射、PC6/7；其CRL读改写只改变6/7位，新增PC4配置也只改变4位。新MSP内GPIO读改写用短临界区，避免被ControlTask的安全关闭抢占后写回旧CRL。SPI传输、数学与启动等待均不在临界区。
- SPI1 PA5/PA6/PA7、PC4为新增所有者；没有其它实际启用代码占用它们。不开SPI1 remap、不写AFIO MAPR；未触碰P1端口、异常、SysTick或FreeRTOS配置。

## 厂商材料实际存在性

- 指定 `Resources/vendor/ICM20602/` 不存在；Resources内没有RAR，因此 **VENDOR_RAR_NOT_IN_REPO**，不能实测/确认设计包给出的RAR hash。
- 实际存在 `Resources/1-HAL ICM20602 Hardware SPI/` 解压目录。本次只核对其中 `Src/SEEKFREE_ICM20602.c`、`Inc/SEEKFREE_ICM20602.h`，没有把“解压源码已核对”写成“原RAR已核对”。
- C SHA-256：`047A574910F339CDCEFEF6192C6E794DEBF25288B73E8264A00C622B491ED284`；H：`9FE214E3C69F99FC6F57636D10A29D9B4745191002C7F22648CA5ECB2BE40A68`。
- 实际看到500ms HAL超时、不查返回值、无界ID/复位等待、`GYRO_CONFIG=0x18`却除131、`ACCEL_CONFIG=0x10`、两次6字节读取、未写I2C_IF。正式模块自主编写，没有复制厂商驱动/HAL/main/启动文件；厂商原目录及版权内容未改。

## 官方寄存器核对及访问限制

资料：[TDK DS-000176 Rev1.1](https://invensense.tdk.com/wp-content/uploads/2020/11/DS-000176-ICM-20602-v1.1.pdf)。本次该URL的直接打开/下载重定向到404，未保存或宣称逐页阅读完整PDF；通过搜索服务返回的**官方域名索引正文**核对了3.1、9.19、9.40等相关段落，且以官方域名Rev1.0的9.16/9.17表交叉核对陀螺配置。具体范围如下：

- GYRO FS_SEL=0：±250dps/131LSB每dps；启动35ms典型、100ms上限，采用唤醒后100tick等待。
- GYRO_CONFIG FCHOICE_B=00、CONFIG DLPF_CFG=3：1kHz内部输出、41Hz带宽。SMPLRT_DIV=9，因此100Hz。
- ACCEL_FCHOICE_B=0、A_DLPF_CFG=3：44.8Hz带宽，1kHz滤波输出经SMPLRT_DIV再分频。
- I2C_IF(0x70) Bit6设1禁用I2C，复位后重新设置。未使用FIFO/IRQ/FSYNC。
- ±2g及16384LSB/g、ID=0x12、burst映射还与冻结设计/厂商寄存器定义交叉核对；实芯片ID、配置回读、采样节拍仍须H01/H02。

## 真实HAL与ARMCC依赖

本仓库HAL_SPI_TransmitReceive在入口取一次HAL_GetTick，8位循环和最终BSY等待共用该tickstart及Timeout。用单次2字节寄存器/15字节burst调用，Timeout=3ms；共享SysTick继续运行且ControlTask可抢占。此为HAL tick预算，不能等同实测WCET或含抢占的硬墙钟上限。错误后释放CS、关SPI、任务FAILED；不调用可能额外等待的HAL_SPI_Abort。

新增实际HAL SPI源第一次链接缺HAL_DMA_Abort/Abort_IT/Start_IT。保留HAL版本/配置/编译选项，在原uvprojx再加入现有HAL DMA源解决库内依赖；应用不使用DMA，最终MAP的有效代码/数据表没有DMA对象贡献，未使能DMA时钟/通道/IRQ。首次失败日志保存在 `evidence/armcc/first_link_failure.log`。

本机uVision注册的编译器显示名是V6.16，但所选路径实际armcc和armlink均为 **5.06 update7 build960**；见compiler_version.txt与MAP首行。没有更换编译器设置。
