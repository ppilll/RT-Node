# 来源与现有代码审查（编制于2026-10-17）

## 现有 main HEAD 与已读源码

`d11fb8b1de2da1a6c18e55970176979b42b36ce7`（P2A），仓库 <https://github.com/ppilll/RT-Node>。本包编制期间读取：

- `App/control/control.c`：ControlTask优先级1、20tick；DWT时序检查、原2ms执行约束；TEST路径在 `#if P1B_AUTOTEST_ENABLE` 下；Stop故障先ForceSafe并terminal；Enable后≥40ticks、DWT时间双条件、Actuator内部UIF。
- `App/control/control_policy.h/.c`：只定义SOURCE_NONE/TEST；单次 `ControlSession_Authorize`；Accept验证源、generation半环、TTL≤100、源issued_tick、范围，非法即永久终止。
- `App/control/control_config.h`：P1B输出测试宏及守卫；默认关闭，`CONTROL_MAX_TTL_TICKS=100`、`CONTROL_PRIMING_TICKS=40`、`CONTROL_PERIOD_TICKS=20`。
- `App/actuator/include/actuator.h`：TIM3/PC6/PC7；Pan1000..2000、Tilt1200..1600、中位1500，Disable正常关闭但保持latch，紧急ForceSafe锁存且只复位清除。
- `App/imu/src/imu.c` & `App/imu/include/imu_types.h`：ImuTask priority0、10ticks，READY/FAILED发布、`Imu_GetSnapshot`一致性复制；字段Roll/Pitch、sample_tick、generation、valid、state。
- `Driver/imu/icm20602.c`：HAL SPI1 Mode3、562.5kHz、CS PC4、14-byte burst、3ms事务超时；不改驱动。
- `doc/P2a/P2a_HANDOFF.md`/`HARDWARE_TEST_GUIDE.md`：BUILD/HOST PASS，H01-H04历史NOT_RUN，用户新增截图是局部时序证据。

## 外部资料（设计依据，非板上测试）

- TDK 官方 ICM20602 DS-000176 Rev1.1：<https://invensense.tdk.com/wp-content/uploads/2020/11/DS-000176-ICM-20602-v1.1.pdf>；其6轴组合不提供绝对航向基准。
- ST STM32F103官方资源页：<https://www.st.com/en/microcontrollers-microprocessors/stm32f103/documentation.html>；RM0008 GPIO/SPI/TIM说明。
- FreeRTOS `xTaskDelayUntil` 任务周期语义：<https://github.com/FreeRTOS/FreeRTOS-Website-Content/blob/main/content/en-us/Documentation/02-Kernel/04-API-references/02-Task-control/03-xTaskDelayUntil.md>。
- 正点原子精英V2板资源资料与ST社区引用：<https://shequ.stmicroelectronics.cn/thread-630862-1-1.html>，KEY0=PE4 / KEY1=PE3、普通低电平按键；**必须在实际开发板核对**，非已验证的当前硬件测量。

## 必须独立核对的事项

1. Codex开工时最新Git HEAD和本地未提交文件/AGENTS规则。
2. 是否已有针对本包 P2b 功能的用户先前本地改动；不得覆盖。
3. KEY0、KEY1引脚/极性及是否存在GPIO占用；纯 C 映射可先行，物理授权不能默认为已测。
4. P2a H01~H04的新实测证据：不能仅凭截图推断全部 PASS。

## P2b实施核查（2026-10-17，实际执行）

- HEAD=`d11fb8b1de2da1a6c18e55970176979b42b36ce7`、main；进入时仅`?? doc/P2b/`。HTTPS `git ls-remote`远端main相同；未提交/推送。
- 仓库递归及F:/AGENTS.md、F:/嵌入式Linux学习资料/AGENTS.md、根AGENTS.md均无文件；未把AGENTS_ROOT_DELTA_PROPOSAL当生效指令。
- 已读本包全部必读文件、P2a_HANDOFF、P1b/interfaces、App/control全部、App/imu/include全部、imu.c、actuator.h、main.c；本轮没有新增P2a硬件证据，旧状态不改。
- 实际打开ST社区上述资源页，按键段落明确KEY0→PE4、KEY1→PE3及内部上拉。只支持候选连接，不证明用户实物V2接线/电平，现场NOT_VERIFIED。
- 搜索App/Core/Driver/imu，原工程无GPIOE/PE3/PE4正常占用；SPI为PA5/6/7+PC4，舵机PC6/7。CMSIS模板GPIOE写入位于未定义的DATA_IN_ExtSRAM条件块，Keil未启用外部SRAM。未发现源码冲突，未猜替代引脚。
- 生产只改Control五文件、新增local_follow.c/.h、p2b_config.h，Keil只加一个C文件。328个受保护已跟踪文件相对HEAD不变，源码hash见evidence/static.json；Actuator、main/异常/时钟、IMU/Driver、FreeRTOS/port均未改。
- 真ARMCC5 5.06 update7 build960，Stage0/1 Keil Rebuild均0 errors/0 warnings；日志V6.16是历史工具别名，真实版本见compiler_version.txt及MAP。
- 真MSVC /W4 /WX /O2编译执行生产C：53次HOST执行通过，12次预期门禁编译拒绝；完整命令/返回码/输出见evidence/host/results.json。LA/机械NOT_RUN，真实WCET/栈余量NOT_MEASURED；Stage2未构建。
