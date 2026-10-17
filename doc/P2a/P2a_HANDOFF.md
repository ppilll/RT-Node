# P2a 交接 — IMPLEMENTED / HARDWARE NOT_RUN

日期/执行者：2026-10-17，Codex单会话。**不建议 P2a ACCEPTED；不允许进入 P2b。**

## 1. 身份与现场保护

- 进入/交接HEAD均为 `c9d48211a11ed60f949e67da49cefff3a9d1a897`，branch=`main`；已用HTTPS核对远端main一致。实现尚在工作树，未提交/推送。固件身份须结合下面HEX hash及`evidence/static.json`的实际源文件SHA-256清单。
- 原未提交`temp1.log`、未跟踪`Resources/`及原`doc/P2a/`文档保留；没有覆盖或改写现有P2a输入文档。temp1.log hash前后相同，见[SOURCE_REVIEW.md](SOURCE_REVIEW.md)。
- 没有实际AGENTS.md。README内缺失的`doc/P2/`基线链接按同包实际基线读取。
- 厂商RAR：`VENDOR_RAR_NOT_IN_REPO`；解压参考源码实际存在且已核对C/H，详见SOURCE_REVIEW，不声称核对了原RAR及其hash。正式源码未复制厂商代码，版权资源未改。

## 2. 实际变更文件与保护

新增正式文件：

- `App/imu/include/imu.h`、`imu_types.h`、`imu_fusion.h`
- `App/imu/src/imu.c`、`imu_fusion.c`
- `Driver/imu/icm20602.h`、`icm20602.c`、`icm20602_data.c`

共享文件最小修改：

- `Core/Src/main.c`：只增加头文件及Control_Init之后、scheduler之前的Imu_Init失败检查，共5行。
- `Projects/RTnode.uvprojx`：增加IMU组4个C文件、2个include路径、已有HAL SPI源及其已有HAL DMA链接依赖。保留原目标/ARMCC/MCU配置及原文件顺序。

新增交付/证据：`tests/fusion_test.c`、`tests/task_driver_test.c`、`tests/stubs/{FreeRTOS.h,task.h,stm32f1xx_hal.h}`；`run_host_tests.py`、`check_static.py`、`rebuild.ps1`；`SOURCE_REVIEW.md`、`HARDWARE_TEST_GUIDE.md`、本文件及`evidence/{static.json,host/results.json,armcc/*}`。

S01 PASS：320个受保护已跟踪文件相对HEAD不变，涵盖Actuator、Control、FreeRTOS/port、startup、interrupts、rtos_hooks、FreeRTOSConfig、HAL配置及现有HAL/CMSIS。main的安全/时钟/SysTick代码保留。新增模块无Actuator Enable/Apply、TIM3写入、AFIO写入或DMA/IT传输。`git diff --check`无新增空白错误。

## 3. 实际技术参数与语义

| 项目 | 实现 |
|---|---|
| 引脚/接口 | SPI1 PA5 SCK、PA6 MISO、PA7 MOSI，PC4软件CS，3.3V；INT不接；FSY=FSYNC待用户最终确认 |
| SPI | 4线主机、Mode3、8bit、MSB、软件NSS、APB2 72MHz /128=562500Hz；CS先预置高再设输出 |
| 初始化 | 可调度上电等100ticks；ID最多2次；复位后100ticks及最多10次复位位检查；再次识别；写配置、等100ticks、有限回读；不是无限重试 |
| 寄存器 | I2C_IF=40，PWR_MGMT_1=01，PWR_MGMT_2=00，CONFIG=03，SMPLRT_DIV=09，GYRO_CONFIG=00，ACCEL_CONFIG=00，ACCEL_CONFIG2=03；FIFO_EN/INT_ENABLE/USER_CTRL=00；全部关键值回读 |
| 原始采集 | 一次HAL调用、CS窗口内MOSI BB+14字节；Ax/Ay/Az/Temp/Gx/Gy/Gz大端二补码；全成功才写Raw出参 |
| 单位 | ±2g、±250dps；16384LSB/g、131LSB/(deg/s)；不输出Yaw |
| 任务 | ImuTask static，priority0，10tick目标周期，512words=2048bytes栈+静态TCB；ControlTask仍priority1/20ticks |
| 校准 | 连续100帧，norm0.8..1.2g、每轴gyro绝对值≤5dps；运动重置窗口，3秒总期限；学习三轴偏置，以校准结束加速度初始化角度 |
| 融合 | 纯C，tau=0.5s，芯片轴系Roll/Pitch；使用成功burst结束的uint32 tick差；dt=0或>50ms时READY转FAILED，NaN/Inf/不合理重力亦拒绝 |
| 调度逾期 | 重新建立10tick等待，避免追赶式连续burst；不调整P1优先级/内核配置 |
| SPI错误 | 单HAL事务3ms共同tick预算，释放CS、禁SPI、发布invalid/FAILED；不会写部分帧；本版失败后不恢复/重连 |
| 快照 | 唯一写者ImuTask；完整结构短临界区复制；每发布generation自然递增；时间戳仅成功burst刷新，SPI失败保留旧时间戳；valid仅READY且估计通过时true |

`Imu_GetSnapshot(NULL)`或尚未发布返回false；失败快照仍可复制。仅任务上下文调用，后续消费者须同时检查state/valid/新鲜度。没有外部UART/调试读出。大角度三维耦合、快速线加速度、温漂精度不在本简单互补滤波承诺内。

初始化自身等待总量≤400ticks（不包括调度抢占）及≤36次有限SPI事务；典型单次ID成功/立即复位完成路径等待300ticks、26次事务。按3ms事务预算粗合计<0.51s，不把它当实机启动耗时或WCET。校准另限3000ticks。

P2a编译单元强制所有P1输出测试模式关闭；主机负向编译已证明误开P1b输出测试会失败。DMA/FIFO/DMP/IRQ未启用；HAL DMA C仅是原HAL SPI的链接依赖。

## 4. 构建/资源证据

工程：`Projects/RTnode.uvprojx`，Target 1；真实ARMCC **5.06 update7 build960**，armlink同版本。Keil显示名V6.16的历史命名与实际工具不同，版本以`evidence/armcc/compiler_version.txt`及MAP首行为准。

| 项目 | 结果/证据 |
|---|---|
| B01 Rebuild/Link | PASS，0 errors / 0 warnings；`evidence/armcc/rebuild.log`、`build.json` |
| Code / RO / RW / ZI | 14274 / 382 / 112 / 5800 bytes；`evidence/armcc/RTnode.map` |
| ROM总量 | 14768 bytes（Code+RO+RW） |
| 静态RAM | 5912 bytes（RW+ZI，含原有startup栈等保留） |
| 相对既有C2默认MAP | ROM 8060→14768，+6708bytes；RAM 3672→5912，+2240bytes；这是文件对比，不是新增硬件证据 |
| ImuTask静态栈 | 2048bytes；静态callgraph报告264bytes+Unknown，不能替代运行栈余量 |
| 运行栈水位、WCET、ControlTask实测节拍 | NOT_MEASURED |

**本次可烧录文件：`doc/P2a/evidence/armcc/RTnode.hex`**

SHA-256：`F37665E188B8F731138FE37DA11BA8E2ACF2E41CE7C91F40A8E70733EFD740D5`。

同目录保留完整日志、MAP、AXF、HEX、LNP、SCT、静态callgraph、版本与hash记录；首次缺HAL DMA符号日志保留为first_link_failure.log。构建脚本在存证后恢复既有Projects输出/偏好文件，故原`Projects/RTnode.hex`仍是此前镜像；请勿混用。

## 5. 测试状态

| ID | 状态 | 实际证据/范围 |
|---|---|---|
| S01 STATIC | PASS | `evidence/static.json`；P1文件、最小main/project变更、源hash |
| B01 BUILD | PASS | `evidence/armcc/rebuild.log`、MAP及HEX |
| U01 HOST | PASS | `evidence/host/results.json`；真实14字节解码，正负/端点，单位换算 |
| U02 HOST | PASS | 真实imu_fusion.c；水平/±15° Roll/Pitch，3秒后最大静态误差约0.039435°，≤2°；还测倾斜启动初始化 |
| U03 HOST | PASS | 真实校准/融合；三轴偏置1.5/-2/0.75dps，60秒水平+7秒已知往返，5/10/15ms变dt；最大Roll误差约0.147076°，≤3°，最终回水平误差<0.1° |
| U04 HOST | PASS | 真实驱动+任务，mock HAL/RTOS：静态创建/重复创建/创建失败、ID错、复位不结束、配置错、SPI超时、半帧、READY后超时、运动校准超时、零重力、dt0/>50ms、tick回绕、逾期等待、完整快照/失败无新鲜时间戳/CS释放/失败后不再SPI；另测NaN/Inf及输出测试宏编译拒绝 |
| H01 LA | NOT_RUN | SPI ID/时钟/Mode3/配置真实回读 |
| H02 LA+MANUAL | NOT_RUN | 实芯片14字节burst、节拍、人工倾斜/回正 |
| H03 LA | NOT_RUN | PC6/PC7上电及持续采集无PWM |
| H04 LA+MANUAL | NOT_RUN | 断电移除IMU再上电，有限失败和无舵机输出 |
| 机械/电压/模拟信号质量 | NOT_RUN | 无现场测试，未编造数据 |

HOST均直接编译生产C；只mock HAL/RTOS，不是伪融合算法。MSVC使用`/W4 /WX /O2`；末尾的C1189是预期负向测试，不是成功镜像的编译错误。主机虚拟时间/CS断言不证明STM32实际时序、抢占或信号电压。

复现命令（仓库根目录，PowerShell）：

```powershell
python doc/P2a/check_static.py
python doc/P2a/run_host_tests.py
./doc/P2a/rebuild.ps1
Get-FileHash doc/P2a/evidence/armcc/RTnode.hex -Algorithm SHA256
```

主机脚本默认vcvars在`D:\VS_IDE`（可设环境变量P2A_VCVARS）；构建默认MDK在`D:\MKD5.36`（可传-Uv4）。Rebuild不会烧录，也不切换为P1测试镜像。若Keil已开则脚本明确退出，避免并发写工程。

## 6. 操作者下一步和边界

按[HARDWARE_TEST_GUIDE.md](HARDWARE_TEST_GUIDE.md)完成断舵机接线、确认FSY、CH340烧录本次证据HEX、H01–H04，并保存原始LA/CSV/截图与操作者记录。没有真实证据前不把任何LA/机械项目升级PASS。RAM中角度/栈统计不是外部已测证据，当前也没有输出精度读出通道。

现有P1历史证据文档缺口保留，不虚构补齐。官方PDF直接URL访问受限及已核对索引段落范围、厂商RAR缺失均已如实记录。

结论：**P2a IMPLEMENTED / HARDWARE NOT_RUN；不是 ACCEPTED；P2b=NO。**
