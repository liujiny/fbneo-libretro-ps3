# H15-C1.1 / C1.2 实机日志与 C1.3 shadow 开销实验

日期：2026-09-19。输入：工作区 `fbneo-ps3-memory1.1.log`、`fbneo-ps3-memory1.2.log`，游戏 kov2。

## 结论与比较限制

当前日志不支持宣称 C1.2 有明确 FPS 提升。用户观察“肉眼差不多”与数据相容。
两次启动/场景时序、负载、长度不同；以下是各自样本的描述统计，不是同场景回放的因果测量。
`avg_frame_us` 来自核心 retro_run 计时，不能直接取倒数当作屏幕显示 FPS。

| 项目 | C1.1 | C1.2 |
| --- | --- | --- |
| 完整 120 帧性能窗口 | 30（3600 帧） | 24（2880 帧） |
| 完整窗口平均核心帧耗时 | 19.762 ms | 20.128 ms |
| 完整窗口平均 sprite draw | 4.758 ms | 5.163 ms |
| sprites_avg_frame ≥20 的窗口数 | 22 | 18 |
| 上述窗口平均核心帧耗时 | 18.976 ms | 19.168 ms |
| 最后 STATS 的 calls | 37,355,520 | 31,850,496 |
| used/calls | 51.129% | 48.503% |
| stream_short_skip/calls | 46.226% | 48.449% |
| abandoned/decoded | 30.507% | 39.389% |
| fresh_use/fresh_dec | 49.732% | 60.741% |
| chain_use/chain_dec | 73.670% | 60.575% |
| fresh 中 lead1 比例 | 99.852% | 99.892% |
| 全量 shadow 比较次数 | 19,099,599 | 15,448,501 |
| shadow_bad / shadow_pixel_bad | 0 / 0 | 0 / 0 |

所有窗口 `frames=120`，因此窗口均值可以直接平均。
最后 STATS 晚于最后完整性能窗口，包含未满 120 帧的尾段；没有把最后 STATS 总量除以完整窗口帧数。
按 sprites 数筛选只能粗分负载，不能保证场景一致；也不能把同 calls 数误认为同帧/同场景。

两份日志分别有 570 / 486 条 STATS。逐条检查均满足：

- `submit - done == inflight`。
- `decoded == used + abandoned + pending`。
- `shadow_bad=shadow_pixel_bad=0`，`lag_max≤3`，所有 lag≥4 桶为零，`chain_fail=0`。

没有发现 SPU 任务丢失或长尾阻塞。短流门槛频繁触发，但也有大量 speculative 数据未被使用，
所以不直接降低 gate 或扩大 batch。fresh 几乎全是 gate12 后 lead1 / batch15；
仅调整 long-stream batch48 很难触及这组日志的主要 fresh 成本。

## 本轮修改

1. `src/burn/drv/pgm/pgm_draw.cpp`：新增 shadow 检查间隔；第一个 SPU 命中检查，之后每 N 个命中检查一次。
   原默认 N=1，性能实验 N=257。257 与颜色计时的 64 次采样周期互质，避免在连续全命中流中固定同步。
   增加 `shadow_interval` 和 `shadow_skipped` 日志；`shadow_cmp` 仍是真实完成的比较次数，不伪装为全量验证。
   未抽样命中直接使用原 SPU 输出，抽样命中仍逐像素比较；原有 bad 计数语义保留。
2. 同一文件：SPU 命中、连续 packed PPU 解码及 scalar fallback 共用计时收尾。
   修复 SPU 命中提前 return 导致 `pgm_perf_in_color_block` 未复位、color-expand 采样漏记的问题。
   因此新旧版本的 color-expand 采样值不可直接比较；帧总耗时、sprite draw 更适合比较。
3. `src/burner/libretro/Makefile`：新增 `PS3_PGM_SPU_SHADOW_INTERVAL ?= 1`，只给 `pgm_draw.o` 增加该宏。
   切换参数后必须重编该对象（make 不跟踪命令行参数变化）；不需要重编 SPU 或 worker。
4. 新增 `tools/test_pgm_spu_shadow.py`，抽取生产路由及采样函数，使用模拟 SPU 验证路径行为。

预期减少约 99.61% 的 shadow reference 解码/比较次数，而非减少同等比例的总帧耗时。
两版保留 C1.2 span 准备优化、gate12 / batch48 / lead3 / chain40 / tail8，协议、DMA 和同步路径不变。
抽样版的 `shadow_bad=0` 仅说明被检查的样本一致，不能表述为全量零差异。
日志应有 `shadow_cmp + shadow_skipped == used`，抽样数量约为 `ceil(used/257)`。

## 构建与验证

全部编译在 `ps3dev-gcc13:rsxfix` 内，沿用交接的全部核心及前端参数，另分别指定 shadow interval。
只增量编译 `pgm_draw.cpp`；两次构建日志中均只有这一条 PPU 编译命令。
切换抽样版时删除并重新生成 `pgm_draw.o/.d`；两次前端重链分别重新生成 ELF/SELF。
没有 clean，旧版命名 SELF 全部保留。

| 产物（根目录） | 核心及 archive | 前端及 SELF |
| --- | --- | --- |
| `RetroArch_FBNeo_PGM_SPU_v1-H15-C1.3_shadow1_gate12_batch48_lead3_chain40_tail8_20260919_0842.self` | 19 秒 | 21 秒 |
| `RetroArch_FBNeo_PGM_SPU_v1-H15-C1.3_shadow257_gate12_batch48_lead3_chain40_tail8_20260919_0843.self` | 22 秒 | 17 秒 |

时间戳是容器 UTC，对应北京时间 16:42 / 16:43。
当前核心/RetroArch archive 和未带版本名的 SELF 为 shadow257；Makefile 默认仍是 shadow1。
编译时没有新增 warning；已有 unused-variable / `.bld` sprintf 警告保留。
本轮 diff whitespace 检查通过，没有整理原有无关空白或代码。

两版均校验 archive 提取的 pgm_draw.o 与编译对象一致，核心 archive 与 RetroArch 拷贝一致：

| SHA-256 对象 | shadow1 | shadow257 |
| --- | --- | --- |
| pgm_draw.o（含 archive 内对象） | `acf78a516f75fa1d955440a878530a2f64c71f118741b6ac831cfae807859b0b` | `b7b2449c2da7582c96bfccc4994f8cec50c2d9cc27fa9504d6a9b4517c0eb16e` |
| 核心 / RetroArch archive | `58e6763b94ce4fd10b1a146fb1af06604f40a0ff80766b48259698374bd4c1b8` | `17a00221c67a3c19f37cb0588d950b97f4fc67a283933d7458cc5a937dc2b773` |
| 命名 SELF | `3dda698742eeb2928e72000d21d222bd92fd4e3ffa1356955e896c3f9b670a11` | `10fbfe865b0eedcb82eb8f77f0cda386388cd9c7150778600e67de91332af23d` |

worker.o 保持 `04aaf173b2e14a1a9b47e4e0b6bcd4e840fb65781fee37a18681bd56572f0687`，
SPU generated header 保持 `b1b2d6cd8f421a0ad3b04246c822af09795047a9378e916e0d13bf612fc024f5`。

验证结果：

- ASan/UBSan 路由测试覆盖 worker/profiler 开关和 interval=1/257，共五种配置。
- 1028 次模拟 SPU 命中：全量 cmp=1028；抽样 cmp=4、skipped=1024。
- 故意注入错误，抽样与全量各自正确记录被检查的错误；输出始终保留 SPU 路由结果。
- 覆盖连续 packed fallback、无 span 的 pair fallback、scalar fallback、ROM 末尾；每次返回计时标志复位。
- 再次执行 batch 测试，441,850 组差分通过；新增日志后 58 个格式参数数量/类型匹配。

日志：`H15-C1.3-shadow{1,257}-{core,retroarch}-build.log`、
`H15-C1.3-shadow-test.log`、`H15-C1.3-batch-test.log`。

## 下一步实机

本轮暂停点是测量真实收益。优先比较 C1.3 shadow1 与 shadow257，二者都有同样的计时修复。
建议进入同一演示段后各运行约 60 秒，保持操作与前端设置一致，再提供日志；不必抄瞬时 FPS。
检查平均帧耗时和 sprite draw 是否下降，同时观察有效使用量与 late/poll 分布。
抽样版让 PPU 更快消费结果，可能改变 SPU 预取提前量需求；拿到此版数据后再决定 lead/tail/gate 调整。
