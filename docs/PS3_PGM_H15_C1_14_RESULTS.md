# H15-C1.13 实机归因与 C1.14 PPU-only

日期：2026-09-19。

## C1.13 实机归因

输入 `fbneo-ps3-memory.log`，SHA-256：
`b0fffa7f8e31f197d2ee623792ddc05e7f472ca835f2e85818268d96df59df7b`。
27 个完整窗口，共 3,240 帧；8 条 stats4m 记录全部守恒。末次 sampled shadow 63,220
次，无差异；submit=done=1,014,891，lag_max=3。

分类耗时闭合：全部窗口 `color_expand=2.015 ms/frame`，SPU 分类 1.015 ms，PPU 分类
1.000 ms；两类之和与 total 只因逐窗口整数除法相差约 0.52 us/frame。

| 指标 | 全部窗口 | sprites/frame ≥20 |
| --- | ---: | ---: |
| SPU sample 占比 | 48.413% | 45.886% |
| SPU hit 平均采样成本 | 0.2014 us/call | 0.2053 us/call |
| PPU fallback 平均采样成本 | 0.1862 us/call | 0.1757 us/call |

平均采样成本的计算是分类外推总时间除以 64，再除以原始样本数。SPU 命中包括 pipeline
查询、cache/result copy 和函数内其余工作；PPU fallback 包括同一 pipeline 尝试以及实际
packed 解包。因此它衡量的是 renderer 看到的两条完整路径，不等于纯 SPU 指令时间。

SPU 命中并不比 PPU fallback 便宜；全部窗口贵约 8.2%，高 sprite 子集贵约 16.8%。
即使第二个 SPU 提高命中率，当前命中路径仍会替换成更贵的工作，而且需要额外队列、DMA
和有序回收。现阶段没有证据支持多 SPU 能提高 FPS。GPU offload 也不能消除立即消费颜色
结果所需的同步，暂不优先。

## C1.14：PPU-only 对照

C1.14 使用相同 cache、fixed-8 unpack、time-base profiler、120 帧窗口和其他编译参数，
只将 `PS3_PGM_SPU_WORKER=0`：

- `pgm_draw.cpp` 不编译 scheduler/poll/shadow 分支；
- `pgm_run.cpp` 不初始化或销毁 SPU worker；
- 所有 packed color 都走已经验证的 PPU fixed-8 helper；
- 帧日志新增 `spu_worker=0/1`，防止只凭文件名误判版本；
- color split 中应为 `color_spu_samples=0`，所有样本进入 PPU。

这是编译配置对照，不删除 SPU 源文件。旧 archive 可能仍含未引用的 worker member；对新
`pgm_draw.o`/`pgm_run.o` 的 undefined-symbol 检查确认没有 `ps3_pgm_spu_*` 引用，链接器不会
拉入 worker。该版本没有运行时 sampled shadow，因为 SPU 路径未编译；PPU fixed-8 helper
本身保持 C1.12/C1.13 实现，并由独立 scalar oracle 验证。

## 验证与增量构建

全部测试和编译在 `ps3dev-gcc13:rsxfix` Docker 内完成。

- worker=0/profile=1 与 worker=0/profile=0 路由通过；
- worker=1 的 interval1/257 组合继续通过，确保源代码没有破坏可恢复的 SPU 基线；
- PPU unpack 随机 oracle 3,000,000 组通过；
- 为切换未被 make 依赖追踪的宏，仅删除并重编 `pgm_draw.o`、`pgm_run.o`、`libretro.o`
  及对应 `.d`；没有 clean，其余对象和缓存保留；
- 核心 22 秒、前端 17 秒、总计 39 秒；三对象与 archive 提取内容一致，archive 副本一致；
- 编译仅有此前已有告警。

测试日志：`H15-C1.14-tests.log`。
构建日志：`H15-C1.14-core-build.log`、`H15-C1.14-retroarch-build.log`。

- `pgm_draw.o`：`0994cd69e3e5d692ae9f69d5bcb619cfd72c029ce824b05078451c75175b4825`
- `pgm_run.o`：`2f17be769093da3d943ecad203182ef7345a62adcd3bc461dd34fe075f204746`
- `libretro.o`：`6447e4364fecad32eb07e88d17315aacdc31babd8bdd94c1f4c04d85f002fc91`
- archive：`81cb80d7a7ef1532310f738aeeac697093194d32154bab466d7d6fed2afa741d`
- SELF：`18d089ad2621f8a22729ffd860d92397f2fb5783a968541bcfb2e80731607fc6`

产物：`RetroArch_FBNeo_PGM_PPU_v1-H15-C1.14_ppu-only_color-split_20260919_1436.self`。

## 实机判定

使用相同场景约一分钟。日志应显示 `spu_worker=0`、`color_spu_samples=0`，且不会出现 SPU
selftest/STATS。重点比较整帧、sprite draw 和 `color_ppu_sampled_avg_us`；同时肉眼观察 FPS。
由于没有 SPU 输出，本版无需检查 shadow，但画面若出现任何差异应立即报告。

如果 PPU-only 明确更快，后续发布方向是移除这条细粒度 SPU pipeline 的运行时成本，并把
SPU/GPU 研究转向可独立完成的大粒度 raster 工作；如果整帧更慢，则恢复 C1.13 并进一步
细分 SPU hit 内部的 poll/cache-copy 成本。
