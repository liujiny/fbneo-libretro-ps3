# H15-C1.11 实机结果与 C1.12 stats throttle

日期：2026-09-19。

## C1.11 实机结果

输入 `fbneo-ps3-memory.log`，SHA-256：
`3a31203bfe1ed446b5a7372d5340238b0303b934ef20bd0496b3069f4f3ffcbe`。
28 个完整窗口，共 3,360 帧；554 条 SPU 统计全部满足任务、结果、shadow 和提交来源守恒。
66,883 次 sampled shadow 比较无差异；submit=done=1,164,610，lag_max=3。

time-base 换算正确：窗口帧耗时仍在约 16–25 ms 的合理范围，audio/video/cache I/O 等量级
也与之前一致，没有固定倍率或回绕错误。

各取前 24 个完整窗口：

| 平均耗时 | C1.7 | C1.10 | C1.11 |
| --- | ---: | ---: | ---: |
| retro_run | 18.965 ms | 19.827 ms | 18.891 ms |
| sprite draw | 4.269 ms | 5.012 ms | 4.217 ms |
| 已记录 cache I/O | 1.126 ms | 未列 | 1.010 ms |
| sprites/frame | 23.042 | 23.667 | 22.083 |

C1.11 相对 C1.7 整帧约低 0.39%，sprite draw 约低 1.23%；但场景并非锁步，
高 sprite 子集基本持平。这只能说明没有退化且方向可保留，不能把小差异全归因于计时器。
C1.11 成为新的保守基线。

## C1.12 修改

C1.11 日志揭示 STATS 频率过高：36,306,944 次 calls 产生 554 条长统计，约每 65,536
calls、每 6 帧写一次。每条包含数十个字段并通过 diagnostic 文件路径落盘，可能造成 I/O、
格式化和帧时间抖动。

C1.12 把 STATS cadence 从 `0x10000` calls 调整为 `0x400000` calls（4,194,304），
频率降低 64 倍。按当前负载，约每 380 帧一条；60 秒测试通常仍有 7–9 个累计快照，
足以检查 shadow、守恒、lag、弃用和最终比例。

`COLOR8 calls` 的首次/每 1,048,576 calls 日志、120 帧性能窗口和 cache 统计保持不变。
SPU 调度、解码、shadow257、time-base profiler、worker、ABI、同步和渲染均不改变。

## 验证与构建

全部测试和编译在 `ps3dev-gcc13:rsxfix` Docker 内完成。

- stream producer ASan/UBSan 差分 441,850 组通过；
- PPU fixed-8 unpack 随机 oracle 3,000,000 组通过；
- 五种 worker/profile/shadow 配置通过；
- 核心仅重编 `pgm_draw.cpp`，18 秒；前端 20 秒；总计 38 秒；没有 clean；
- 对象与 archive 提取一致，archive 副本一致；SPU ELF/header/worker.o 哈希未变；
- 编译只有此前已有告警。

测试日志：`H15-C1.12-tests.log`。
构建日志：`H15-C1.12-core-build.log`、`H15-C1.12-retroarch-build.log`。

- `pgm_draw.o`：`7db46a4afefdcb2b9052df31d9a347a9718ce9ddaf4e77ddaf1b978b899aaed2`
- archive：`36a1a4ff8c1e45474e52cc46466c8f9cf69e3a892d97ec55c308b8039dba23ad`
- SELF：`0a6b384949f0c17f4a1acc39d0d6c93fcfd86e61011c28dc360dfef896bc022d`

产物：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.12_stats4m_tb-profiler_shadow257_20260919_1427.self`。

## 实机判定

使用相同设置、场景和时长。预期日志体积和 STATS 数量显著下降；只要测试超过约 400 帧，
应至少出现一条 `C1.12 stats4m`。比较整帧、max frame 和肉眼流畅度；累计计数仍必须守恒，
sampled shadow 两项必须为零。
