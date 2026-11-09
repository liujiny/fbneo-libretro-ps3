# H15-C1.6 实机分析与 C1.7 batch range

日期：2026-09-19。输入：`fbneo-ps3-memory-1.6.log`。
日志 SHA-256：`3d24fcab52473687dbe490ab4d74048efe8250197b9ef0f654cd598b750064d2`。

## C1.6 交付补记

C1.6 把 PPU 连续 packed-color span 按三种 phase 展开，每组读取三个或四个 packed word；
PPU fallback 和抽样 shadow reference 使用同一个 helper。跨页和 ROM 尾部保持原路径。
SPU 解码实现不同；native 测试另外使用独立逐像素 oracle，不只依赖 shadow 比较。

- Docker ASan/UBSan：3,000,000 组随机 unpack 用例，五种 worker/profile/shadow 配置通过。
- Docker 增量构建：核心 21 秒，前端 17 秒，仅重编核心 `pgm_draw.cpp`，不重编 SPU/worker。
- SELF：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.6_ppu-unpack_shadow257_20260919_0921.self`。
- SELF SHA-256：`3af59f6287012e2001c4ee3b2568f29f0f5aeac5f0553e2c0109f68fb2e2a112`。

## 实机结果

日志含 257 个完整 120 帧窗口，共 30,840 帧，3,951 条 SPU 统计。
全部统计满足 `submit - done == inflight`、
`decoded == used + abandoned + pending`、`shadow_cmp + shadow_skipped == used`。
所有记录 `shadow_bad=0 shadow_pixel_bad=0`，末次抽样比较 349,262 组。
末次 `submit=done=7,868,217`，`lag_max=4`，只有三次 lag 落在 4–7，8 及以上为零。
这是抽样验证结果，不代表对本次每个 SPU 输出做了比较。

| 指标 | 全部窗口 | sprites/frame ≥20 的窗口 |
| --- | ---: | ---: |
| 窗口数 | 257 | 120 |
| 平均 retro_run 壁钟耗时 | 18.301 ms | 18.579 ms |
| sprite draw | 3.336 ms | 3.881 ms |
| CPU video callback | 0.956 ms | 0.528 ms |

末次累计比率：

- `stream_short_skip/calls=60.986%`；gate12 fresh 提交 3,841,068 次。
- `used/calls=34.665%`，`poll_pending/calls=2.865%`。
- `late_skip/decoded=2.712%`，`abandoned/decoded=54.677%`。
- `chain_use/chain_dec=41.507%`。

本次约 9 分钟的记录显著长于旧版记录，包含不同场景。即使只截前 32 窗口，
sprites≥20 子集也有 26 窗口、平均 sprites=28.731，并非旧版同场景锁步数据。
因此不能把总体均值差直接归因于 C1.6，也不能把 retro_run 耗时倒数当作实际呈现 FPS。
这些计数提示短流覆盖和预解码弃用值得优先处理，不证明增加 SPU 必然无效。

## 测量限制与多 SPU / GPU

- `poll_pending/calls` 是调用比例，不是 CPU 等待时间或 SPU 利用率，不能用它限制双 SPU 收益。
- `video_cb_avg_us` 是 CPU 回调壁钟耗时，不是 GPU 总执行时间；前端可能异步提交。
- 抽样 `color_expand_sampled_avg_us` 均值 5.684 ms，大于完整 sprite draw 3.336 ms，
  不能按可相加的独立阶段解释。源码每 64 次固定采样，并乘 64 外推；SPU 日志每 65,536
  次输出位于被测调用内，两者存在相位重合风险，计时开销和非均匀调用也可能影响估计。
  暂不以这个估计数预测 FPS 收益，后续需要独立采样或批量阶段计时。
- 多 SPU 候选仍是独立任务队列和有序回收；单纯在当前单 inflight、短流预测基础上
  增加预取深度可能扩大无效工作。需先建立更可靠的有效工作粒度和队列时间测量。
- 当前颜色输出随即由 PPU raster 使用；仅将解包放到 GPU 还需处理结果回传/等待。
  更大粒度的图层合成是另一条候选路径，但需要独立评估优先级、透明和缩放语义及同步成本。
  本轮没有引入多 SPU 或 GPU offload，不修改 Cell/RSX 同步。

## C1.7 实现与预期

生产代码仅修改 `src/burn/drv/pgm/pgm_draw.cpp` 的 batch builder 和版本日志：

- 完整、同页且不超过 ROM/mask 尾部的批次，一次检查整个范围后直接采用 batch_limit。
- 避免普通 15/16/32/40/48 组提交时逐项进行 pixel/phase/page 检查；测试可观测到逐项循环为零次。
- 跨页、ROM/mask 尾部仍走原逐项路径，保持返回 count、payload、DMA 长度与提交位置。
- 保留 C1.6 PPU unpack、gate12、fresh/chain batch、lead、tail8、shadow257。
- 不修改 SPU ABI、worker、mailbox、fence、DMA 同步或模拟器核心逻辑。

预期是降低 PPU 侧提交准备成本，缩短供给 SPU 之前的串行工作，不保证提高命中率或 FPS。
边界路径增加一次快路径判定成本，净收益仍需同场景实机测量。

辅助修改：新增 `tools/analyze_pgm_perf.py`；stream 测试增加逐项循环次数断言；
unpack 测试说明改为随机测试；纠正 C1.5 文档对 pending 和 video callback 的过强解释。

## C1.7 验证、构建与产物

全部编译在 `ps3dev-gcc13:rsxfix` Docker 内进行，无 clean：

- stream producer 差分 ASan/UBSan：441,850 组，覆盖 RAM/file-cache、phase、batch 0–64、
  页边界、ROM/mask 尾部及随机地址；对比旧 builder 的 count 与独立解码输出。
  该测试验证 PPU builder，不执行实际 SPU 指令。
- 3,000,000 组 PPU unpack 随机 oracle 测试和五种 shadow/router 配置通过。
- 核心增量 22 秒，前端 17 秒，总计 39 秒；核心只重编 `pgm_draw.cpp`。
- archive 提取的 `pgm_draw.o` 与原对象一致；复制到 RetroArch 的 archive 哈希一致。
- SPU ELF、generated header、worker.o 构建前后哈希完全不变。
- 已有 unused-variable / `.bld` format-overflow 告警仍保留，未添加相关新告警。
  整体 dirty diff 仍有此前留下的行末空白，本轮不做无关清理。

对象 SHA-256：`7c641afb138ffd805241c4ad934a00f50b1cd7d3f381f70d2d27ed98b51259f0`。
archive SHA-256：`7836b61066502795658880e32637be515616e892a3e03d01c9ddccb279cd04b6`。
SELF SHA-256：`cd39c116974dc5b8b43cc36af764329db96f2853e71f8978915cd037121b7615`。

产物：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.7_batch-range_shadow257_20260919_1330.self`。
构建日志：`H15-C1.7-core-build.log`、`H15-C1.7-retroarch-build.log`。
测试日志：`H15-C1.7-tests.log`、`H15-C1.7-stream-test.log`。

下一步是实际帧率评估：C1.6/C1.7 使用相同设置和起始场景，尽可能复用相同输入或演示段，
保持相同时长（建议每次至少 60 秒）并分别保存日志。此处暂停是因为净 FPS 收益不能离线确认，
不是要求重复已经通过的基础 DMA 正确性测试。

复现日志统计：`rtk proxy python3 tools/analyze_pgm_perf.py fbneo-ps3-memory-1.6.log`。
