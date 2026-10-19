> **P7 安全更正（2026-10-19）：本页是历史 P2b/P3b 设计/测试记录，按键授权步骤不适用于当前 P5/P6 源码。当前 KEY0(PE4)=STOP 且锁存至复位，KEY1(PE3)=OLED 翻页；不得按 KEY0 启动、不得用 KEY1 停机。控制授权服从 P6 持久锁和现有 MODE 策略。Stage1/Stage2 本轮未授权，当前操作以 [P7 现场指南](../P7/operator_runbook.md) 为准。**

# P3b Stage1 无负载交付与 RK3588 操作

只做无负载 PWM 验证：**舵机电源和信号线全部断开**，逻辑分析仪接 PC6/Pan、PC7/Tilt、GND。保持 KEY0 松开再复位。Stage2 未获本轮授权，不编译、不烧录、不带载执行。

## 固件与构建

- 本轮无负载固件：[P3b_CAN500k_STAGE1_T2_T4.hex](evidence/stage1/P3b_CAN500k_STAGE1_T2_T4.hex)。按既有 STM32 串口下载方法烧录此文件，恢复正常启动并复位。Codex 未烧录或操作实机。
- 默认无输出固件：[P3b_CAN500k_STAGE0_T2_T4.hex](evidence/stage0/P3b_CAN500k_STAGE0_T2_T4.hex)。Stage0 可验证协议/WAIT/STOP/TTL，任何按键或目标都不产生 PWM；不能用它验收 ACTIVE/PWM。
- `Projects/RTnode.uvprojx` 保持默认配置，`p3b_config.h` 默认 `P3B_OUTPUT_STAGE=0`。不要误选 Projects 中保留的历史 HEX。

在仓库根目录复现：

```powershell
./doc/P3b/rebuild.ps1 -Stage 0
./doc/P3b/rebuild.ps1 -Stage 1
python doc/P3b/run_host_tests.py
```

构建脚本仅允许 0/1，调用现有 Keil5/ARMCC5 完整 Rebuild，不下载。日志、编译器版本、构建参数、HEX 和 MAP 在对应 `evidence/stage0`、`stage1`。默认工程和已有 Projects 输出随后恢复；`build_project.uvprojx` 是以 Projects 为相对路径基准的审计副本，复现应运行脚本。

## RK3588 准备

使用现有 Python3 与 can-utils，把 [rk3588_test.py](rk3588_test.py) 复制到 RK3588。它使用系统 SocketCAN，无第三方依赖。以下命令在 RK3588 运行，必要时使用已有 CAN 权限或 `sudo`。

```sh
ip -details -statistics link show can0
# 仅在需要重新配置、且确认没有其它 CAN 使用者时执行：
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 sample-point 0.833333 fd off
sudo ip link set can0 up
ip -details -statistics link show can0
```

另开终端记录所有协议帧（Ctrl-C 结束，T2/T3/T4 可各存一份）：

```sh
candump -L can0,180:7FC,280:7FF | tee p3b-stage1.log
```

只运行一个控制发送进程。脚本默认从真实 STATUS 的 last_action+1 开始；若此前发送过旧 action STOP 或存在其它发送者，last_action 不代表最高 action fence，应以 `--action N` 指定**未用过的新 action**，如 `python3 rk3588_test.py --action 20 remote`。不要靠复用旧 action 修正 epoch。模式请求被拒绝时脚本报错退出，发送一次尽力 STOP；断链时依赖 MCU 远程 TTL，不能把本机发送成功当作 STOP 已到达。

## T2：中心、两轴小目标、PRIMING、限速

```sh
python3 rk3588_test.py status
python3 rk3588_test.py remote --seconds 3 --pan 1510 --tilt 1500
python3 rk3588_test.py remote --seconds 3 --pan 1500 --tilt 1510
python3 rk3588_test.py remote --seconds 3 --pan 1500 --tilt 1490
```

先读冷启动 STATUS：state0/epoch0/bit0=0，两线 LOW。脚本请求 REMOTE，等待真实确认（WAIT_REMOTE），再每20ms递增seq；第一秒中心1500，然后给小目标，结束发送 GLOBAL_STOP 并等待禁用确认。若开始时已在 REMOTE，脚本先请求 DISABLED，以建立新的 seq/epoch 会话。

LA 确认：MODE 本身无 PWM；首帧中心；Enable 后至少40ms PRIMING（保留驱动初始 LOW 帧与 UIF 检查）；50Hz；PC6/PC7独立；生效后的每周期每轴目标变化不超过2µs。STATUS enabled/ACTIVE 仅证明软件状态，不代替波形。脚本只有 ±10µs 范围，不做自动大幅扫描。绝对范围在真实 Policy/Actuator 中保留；范围边界已做主机决策检查，物理限幅仍需 LA 按 `test_plan.md` 核验。

## T3：运行中切 LOCAL、KEY0、新边沿与离线

```sh
python3 rk3588_test.py remote --seconds 3 --end local
```

运行中由 REMOTE 切到 WAIT_LOCAL，先 Disable、epoch+1。即使 KEY0 此前已经按住，也必须在 IMU READY 后稳定松开再按下；两轮20ms采样去抖。授权后先 PRIMING_LOCAL，再 ACTIVE_LOCAL。轻微改变 IMU 姿态验证本地跟随；停止发送心跳超过500ms，bit2应清零，LOCAL 应继续。脚本切 LOCAL 后退出，不再发心跳。

LOCAL 活跃时运行 `python3 rk3588_test.py remote --seconds 3`，验证先禁用本地、更新 epoch，只有新目标使能 REMOTE。也可无 CAN 发送者、冷启动 DISABLED 时仅用新 KEY0 边沿验证离线启动。完整旧epoch/按键操作按 [test_plan.md](test_plan.md) 的 T3 记录。

## T4：STOP、TTL、重授权

```sh
python3 rk3588_test.py remote --seconds 3 --end timeout
```

脚本停发 CONTROL 后继续约0.6s HEARTBEAT，要求观察 state0/reason03/bit0=0/bit2=1；用 LA 核验输出在 `last_valid_rx+100ms` 后的 Control 周期停下。脚本正常完成后不再发 STOP，保留 reason03 供观察。恢复旧epoch控制不得重启；重新运行 remote 才重新模式授权。

T2 默认结束路径已验证 REMOTE GLOBAL_STOP 的 STATUS 关联。LOCAL ACTIVE 时执行：

```sh
python3 rk3588_test.py stop
```

确认 GLOBAL_STOP 对 LOCAL 也有效（reason02），KEY0 需重新释放→按下。REMOTE 活跃期间另手动按 KEY1，确认两次采样后禁用（reason01）；脚本发现意外停机将退出，不会自动重新授权。旧 action STOP 仍须有效，可依据冻结接口手动发 `cansend can0 182#0101000000000000`，随后模式请求用已知更新的 action。

真实 fatal latch 不要求人为注入；本轮只有主机锁存与代码检查，不能记为硬件故障实测。RX overflow 也是主机注入，硬件注入未执行。若看到 state7，记录现象并先查故障，普通 STOP/MODE 不会清除它。

## 结果回填与停点

在 [P3b_HANDOFF.md](P3b_HANDOFF.md) 记录 T2/T3/T4 的实际 PASS/FAIL、少量 candump 和 LA 截图。当前全部 `NOT_TESTED`，编译/主机测试不算实机通过；WCET、栈余量、ISR响应也未实测。T2～T4 Stage1 通过后仍须用户另行明确批准才能做有限带载。P3b 尚未 ACCEPTED，不进入 P4。
