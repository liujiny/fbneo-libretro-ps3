# H15-C1.12 实机结果与 C1.13 color split

日期：2026-09-19。

## C1.12 实机结果

输入 `fbneo-ps3-memory.log`，SHA-256：
`6a0040805e29670db7f767bd4c23039bf0d3a17425a17bd636eaeddcfc718b7f`。
31 个完整窗口，共 3,720 帧。日志从 C1.11 的约 560 KiB 降到约 46 KiB；SPU STATS
从 554 条降至 9 条，符合 4,194,304 calls cadence。所有 9 条记录满足任务、结果、shadow
和提交来源守恒；末次 67,647 次 sampled shadow 比较无差异，lag_max=3。

| 全部窗口均值 | C1.11（28） | C1.12（31） |
| --- | ---: | ---: |
| retro_run | 18.752 ms | 18.760 ms |
| sprite draw | 4.109 ms | 4.108 ms |
| color_expand sampled | 5.674 ms | 1.971 ms |
| sprites/frame | 22.964 | 23.871 |

整帧和 sprite draw 基本相同；节流没有确认平均 FPS 收益。color_expand 下降约 65%，
说明旧采样经常覆盖 STATS 格式化/落盘时间，不能用于热路径判断。C1.12 的 1.971 ms
才是当前更可信的颜色处理量级。窗口 max 的中位数约 60.3 ms，p90 约 90.0 ms；
由于没有保存 C1.11 的逐窗口 max，不能严谨归因节流对长帧的影响。

## C1.13 修改

保持 C1.12 所有调度、解码、time-base profiler 和 stats4m 不变，只拆分 color8 的已有
1/64 采样：

- `color_spu_sampled_avg_us`：SPU cache/job 命中并直接返回结果的累计外推耗时/帧；
- `color_ppu_sampled_avg_us`：SPU 未服务、落入 PPU packed/scalar fallback 的累计外推耗时/帧；
- `color_spu_samples`、`color_ppu_samples`：该 120 帧窗口中两类原始采样数，未乘 64；
- 原 `color_expand_sampled_avg_us` 保留，理论上等于两类平均耗时之和（整数除法可差 1）。

两类计时使用同一个起止 time-base 和 cache I/O 扣除逻辑，没有额外读取时钟；每个采样
只多一次分类累加。因此不会把两个阶段误当成并行时间，也不把 sample ratio 当成耗时比例。
此版本用于决定后续方向：若 PPU fallback 时间占主导，优化 PPU/提高有效覆盖；若 SPU hit
本身时间较大，优先处理 scheduler/poll/cache copy；只有确认可并行工作量后才实施多 SPU。

## 验证与构建

所有测试和编译在 `ps3dev-gcc13:rsxfix` Docker 中完成。

- 五种 worker/profile/shadow 配置通过，测试检查 total samples 等于 SPU+PPU 分类；
- stream producer 差分 441,850 组、PPU unpack 随机 oracle 3,000,000 组通过；
- 核心因公共 profiler header 增量重编相关对象，22 秒；前端 16 秒；总计 39 秒；
- `pgm_draw.o`、`ps3_memory_pool.o` 均与 archive 提取对象一致，archive 副本一致；
- SPU ELF/header/worker.o 构建前后哈希未变；已有编译告警保持不变。

测试日志：`H15-C1.13-tests.log`。
构建日志：`H15-C1.13-core-build.log`、`H15-C1.13-retroarch-build.log`。

- `pgm_draw.o`：`5ebe780b62f07565cccb2894f815795c1350f925dd716805da83069cea4aa614`
- `ps3_memory_pool.o`：`59c1a40c4f11abc62df118bd7e1dbe2abaf04a5cb288ae71e323cef6e9d1b82f`
- archive：`fe0b4f80628baacaa69a8e944d656aaa3c8469bd61c877f8d8af5ad0ae2884b7`
- SELF：`5f7130212bc846404b223dc6dfc35fd06bbdb31ef78d3aca67fe8684466fe9ac`

产物：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.13_color-split_stats4m_shadow257_20260919_1432.self`。

## 实机判定

使用相同场景约一分钟。确认新四个字段存在、两类样本数之和合理、分类耗时之和与 total
吻合；同时继续检查 shadow 和任务守恒。该版本本身不是预期 FPS 提升版本，而是用低额外
成本得到下一项高收益优化所需的归因数据。
