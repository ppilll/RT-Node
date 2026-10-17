# RT-Node — P2a / ICM20602 SPI 数据采集与姿态估计

**这是供 Codex 独立执行的文档包，不是已经实施的源码。** 日期 2026-10-17。

## 必读顺序

1. `../P2/P2_DESIGN_BASELINE_V1.0.md` — 总体冻结方案及 P1 不变量。
2. `requirements.md` — P2a 做什么/禁止做什么。
3. `hardware_guide.md` — 模块实物接线与逻辑分析仪。
4. `design.md` — SPI/RTOS/算法设计与精简错误处理。
5. `interfaces.md` — 模块、快照、单位及线程安全契约。
6. `vendor_review.md` — 厂家 RAR 已审查事实和不可照搬处。
7. `implementation_plan.md` — 分阶段编写与检查。
8. `test_plan.md` — 主机与实机验收。
9. `codex_tasks.md` — **可以直接复制的 Codex 启动提示词**。
10. `handoff_template.md` — Codex 和操作者回填的交接模板。

## 开始前

- 确认当前 GitHub `main` HEAD；设计审查基线为 `c9d48211a11ed60f949e67da49cefff3a9d1a897`。有后续提交先比较差异，不能强行 reset/rebase 或覆盖现场代码。
- 用户将原始 RAR 放进仓库 `Resources/vendor/ICM20602/` **是后续计划**。Codex 必须检查文件实际是否存在；不存在不能宣称已核对，也无需等待即可按本包+官方资料实现，标记该项缺失。
- 当前用户无 ST-Link/J-Link，实机验证只依赖 CH340 下载、逻辑分析仪、主机 C 测试和用户操作。无 UART 应用层日志的已验证条件。
- P1 已有测试历史报告存在文档缺口；不重复无关测试，也不把历史自报 PASS 代替本次 P2a 实机证据。

## 本轮交付结果

新的 ICM20602 SPI 驱动、简单 Roll/Pitch 融合、ImuTask/静态快照与 Keil 工程集成；ARMCC 构建和主机测试；待用户运行的最小逻辑分析仪步骤；`P2a_HANDOFF.md` 草稿及证据状态。

**绝对边界：P2a 不使能舵机、不实现 LOCAL_FOLLOW、不动 Actuator、不得自动开始 P2b。**
