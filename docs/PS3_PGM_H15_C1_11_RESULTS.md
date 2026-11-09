# H15-C1.10 实机结果与 C1.11 time-base profiler

日期：2026-09-19。

## C1.10 实机结果

输入 `fbneo-ps3-memory.log`，SHA-256：
`30f4cae33cf4f64e24b57770aaa24dadb049c05051c1666f991a2a0db655ba7c`。
24 个完整窗口，共 2,880 帧；484 条统计的任务、结果、shadow 和提交来源全部守恒。
56,047 次 sampled shadow 比较无差异，lag_max=3。

| 前 24 窗口均值 | C1.7 | C1.9 | C1.10 |
| --- | ---: | ---: | ---: |
| retro_run | 18.965 ms | 19.209 ms | 19.827 ms |
| sprite draw | 4.269 ms | 4.466 ms | 5.012 ms |
| sprites/frame | 23.042 | 22.667 | 23.667 |

C1.10 的固定 8 解包改为部分标量循环后明显更慢；较高 sprite 数只能解释部分差异。
该优化不保留，C1.11 恢复 C1.7 的固定 8 展开和调度路径。

## 发现的测量开销

此前 `ps3_perf_now_us()` 调用 `cpu_features_get_time_usec()`；在 PSL1GHT 下后者直接调用
`sysGetSystemTime()`，即一次 LV2 syscall。性能构建在以下位置反复调用它：

- 每帧前后；audio/video callback 前后；
- 每个 sprite draw 前后；sprite raster 抽样前后；
- 每 64 次 color8 调用前后；另有 cache I/O 计时。

C1.10 约 31,719,424 次 color8 / 2,880 帧，即约 11,014 calls/frame。
仅 color8 的 1/64 抽样平均就触发约 344 次计时调用/帧，尚未计入其余阶段。
因此 profiler 自身可能是比单次解包更大的热路径负担，也会污染正在比较的耗时。

## C1.11 修改

`ps3_perf_now_us()` 改为读取 PPU 79.8 MHz time base：

`microseconds = __mftb() * 10 / 798`

PS3DEV 的 SPU timer 头同样给出 PS3 频率 79,800,000 Hz；使用 `ppu_intrinsics.h` 的
64 位 `__mftb()`。日志单位、所有采样位置和统计结构不变。反汇编确认实现是 time-base
读取、乘法以及编译器把常量除法化成的 multiply-high/shift，没有时间 syscall 或函数调用。

C1.11 同时完全回退 C1.10 部分解包，恢复固定 8 helper；调度是 C1.7 基线。
SPU ABI、SPU image、worker、DMA、mailbox、fence、RSX、像素输出均未修改。

## 验证与构建

全部测试和编译在 `ps3dev-gcc13:rsxfix` Docker 内完成。

- stream producer ASan/UBSan 差分 441,850 组通过；
- PPU fixed-8 unpack 独立随机 oracle 3,000,000 组通过；
- 五种 worker/profile/shadow 配置通过；
- 核心只重编 `pgm_draw.cpp` 和 `ps3_memory_pool.c`，18 秒；前端 20 秒；总计 38 秒；
- 两个对象均与 archive 提取内容一致，archive 副本一致；
- SPU ELF、generated header、worker.o 构建前后哈希不变；
- 仅保留此前已有的 unused-variable 和 `.bld` format-overflow 告警。

测试日志：`H15-C1.11-tests.log`。
构建日志：`H15-C1.11-core-build.log`、`H15-C1.11-retroarch-build.log`。

- `pgm_draw.o`：`b5e0d886ae331e6ccfe832954a62c9429761f01a16fb854b2240a46d3281cd4e`
- `ps3_memory_pool.o`：`1725eb2f11cfb052e78e6376d6f7192af777d8520b866257bb76c83e541f9eb4`
- archive：`f0ff6c670e891a9c30af5c1206bcba16cb90bba3566899975402e69dfd1ca18d`
- SELF：`54305081fe4d7548336b12ca3b4f7b8805657a1f5bd9ad26462437b872606ad5`

产物：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.11_tb-profiler_shadow257_20260919_1423.self`。

## 实机判定

这次修改的是所有性能采样的时钟来源，必须实机确认日志量级合理：帧耗时应仍约 16–25 ms，
不能出现固定倍率错误或倒退；shadow 和任务计数继续守恒。若时间量级正确，比较相同场景的
整帧和 sprite draw，并观察肉眼 FPS。预期收益来自删除 profiler syscall，而非 SPU 解码变化。
