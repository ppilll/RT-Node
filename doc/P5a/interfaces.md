# P5a 冻结接口（建议实现签名）

公开环境快照建议采用 ARMCC5 可编译的 C 结构（所有数值均为**补偿后**，在 P5a 首次运行中核对单位）：

```c
typedef struct {
    int32_t  temperature_centi_c;  /* 0.01 °C，负值允许 */
    uint32_t humidity_centi_pct;   /* 0.01 %RH，0..10000 */
    uint32_t pressure_pa;          /* Pa，例如正常海平面附近约 101325 */
    uint32_t sample_tick;          /* 最后一次成功采集的 FreeRTOS 1 kHz tick */
    bool     valid;                /* 最新一轮采集成功且数据有效 */
} EnvSnapshot;

bool EnvApp_Init(void);                 /* 只创建静态 SensorTask/初始化软件对象 */
bool EnvApp_GetSnapshot(EnvSnapshot *out); /* task context 完整复制；false=尚未发布/空指针 */
```

`sample_tick` 仅在成功采集后更新；若后续通信失败，保留最后成功 tick，将 `valid` 改为 false。不可把 `valid=true` 的历史数据假装新采样。读取者自行计算 `(uint32_t)(now-sample_tick)` 并判断陈旧性（建议 UI 阈值 >2s 显示 STALE）。

**数据换算：** Bosch API double/64bit/32bit 的原始数据单位并不相同。Codex 必须在选定具体 Bosch 版本和 `BME280_...ENABLE` 编译宏后，写明 temp / humidity / pressure 到该公开结构的严格转换规则，并以 T1 输出核对。所有转换使用明确边界/溢出检查。

**UART 命令：** `ENV?\n`（CRLF 容忍现有解析方式），由 `UartTask` 返回一行简洁 ASCII，例如 `ENV:T=25.34C,H=50.12%,P=101325Pa\r\n`；未有快照或无效时 `ENV NOT READY\r\n` / `ENV ERROR\r\n`（实现选一种明确记录）。格式仅为候选，保持现有最大全行缓冲及有界分段 TX；必须估算最大负温度、湿度、六位 Pa 等字段时的缓冲大小。

UART 不新增任务/队列，也不允许 SensorTask 直接 `HAL_UART_Transmit`。原 `PING`、`COUNT?`、`ADC?` 行为和串口参数不变。
