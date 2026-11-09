# H15-C1.4 实机结果与 C1.5 stream unroll

日期：2026-09-19。C1.4 日志：`fbneo-ps3-memory.log`；基线：`fbneo-ps3-memory-shadow257.log`。

## C1.4 实机结论

ABI、自检和解码正确：SPU image 13086 bytes，selftest/DMA test PASS，首次 stream job 为
`count=15 bytes=96`，抽样 `shadow_bad=0 shadow_pixel_bad=0`。所有 STATS 满足：

- `submit - done == inflight`；
- `decoded == used + abandoned + pending`；
- `shadow_cmp + shadow_skipped == used`。

但 C1.4 没有带来 FPS 收益，并显著增加 SPU 延迟：

| 指标 | C1.3 shadow257 | C1.4 stream DMA |
| --- | ---: | ---: |
| 120 帧窗口 | 28 | 35 |
| 全部窗口平均帧耗时 | 19.279 ms | 19.172 ms |
| sprites/frame ≥20 窗口 | 22 | 28 |
| 高负载平均帧耗时 | 18.416 ms | 18.519 ms |
| 高负载 sprite draw | 3.729 ms | 3.938 ms |
| pending/calls | 3.36% | 9.86% |
| late_skip/decoded | 3.41% | 8.95% |
| abandoned/decoded | 39.14% | 50.99% |
| chain_use/chain_dec | 61.16% | 46.70% |
| lag_max | 4 | 8 |

两次演示时序和长度并非严格锁步，帧耗时差异不应过度解读；pipeline 指标的退化幅度足以判定
C1.4 的 SPU decode 变慢。DMA 从 1552/496 字节降至 272/96 字节是有效的，但连续逐像素循环
失去了旧版固定 8 次循环的编译展开，计算开销抵消了 DMA 收益。

## C1.5 修改

保留 C1.4 stream ABI 和小 DMA。SPU 改为：

- 外层按 8-pixel group 迭代；
- 内层固定 8 次，并强制 inline，使 spu-gcc 完全展开；
- 每组直接写连续 result buffer；
- 组间只更新 packed byte offset 和三态 phase。

反汇编确认 8-pixel 解码展开为直线 shift/mask/store 序列，只有外层 group/phase 控制分支。
SPU ELF 从 C1.4 的 13086 bytes 增至 13854 bytes，仍远低于 256 KiB LS。

## 验证和构建

- stream 差分：441,850 组通过 ASan/UBSan；命令 count/phase 编解码和最大 DMA size 也逐项验证。
- shadow/router：五种 worker/profile/interval 配置通过。
- Docker 增量构建：核心 21 秒，RetroArch 21 秒。
- `pgm_draw.o` 与 archive：`158df8e54cc8c684e5305e9280d07c96c7c3227e34053831a04bd2471e1fbad9`。
- worker.o 与 archive：`023ccc2e024d7afdd5439ba4c0965dd9b1f3231c004a963a6e1774a0707c39f9`。
- core/RetroArch archive：`1149f1f754c2b8d613f1692b2a7722bb3a2ba4a18dd57d49ef957565b6b2aa6d`。

产物：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.5_stream-unroll_shadow257_gate12_batch48_lead3_chain40_tail8_20260919_0908.self`。

C1.5 与 C1.4 使用相同 ABI，但改变 SPU 热循环，需要实机对比 pending、lag 和帧耗时。
如果仍明显差于 C1.3，将回到 C1.3 item decoder 基线；不会在退化的 stream 路径上叠加双 SPU。
如果恢复并超过 C1.3，再以 two-ahead reorder queue 方式实现双 SPU，而不是简单增加第二线程。

## C1.5 实机结果与 GPU 路径测量

最新 `fbneo-ps3-memory.log` 确认为 C1.5（SPU image 13854 bytes，首个 stream job 96 bytes）。
正确性、任务守恒和 shadow 抽样全部通过。C1.5 相对 C1.3：

| 指标 | C1.3 | C1.5 |
| --- | ---: | ---: |
| 高负载平均帧耗时 | 18.416 ms | 18.450 ms |
| 高负载 sprite draw | 3.729 ms | 3.693 ms |
| pending/calls | 3.36% | 2.28% |
| late_skip/decoded | 3.41% | 2.44% |
| abandoned/decoded | 39.14% | 36.18% |
| chain_use/chain_dec | 61.16% | 65.15% |
| lag_max | 4 | 3 |

总帧耗时可视为持平；pipeline 吞吐和利用率有明确改善，C1.5 成为新基线。

pending/calls 为调用比例，不是耗时比例，不能据此给出双 SPU 收益上限。完整 two-ahead
需要双 buffer、token、有序回收和 stream-break 作废。先测量 frontend 回调：新增
`PS3_PERF_VIDEO`，只在 `video_cb` 前后取时间，每 120 帧随原性能行输出
`video_cb_avg_us`。它只度量 CPU 端回调壁钟耗时；是否覆盖等待取决于前端运行路径，
不能当作 GPU 执行总耗时，也不能排除 GPU offload 收益。不改变渲染、SPU 或 pipeline 参数。

测量产物：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.5_video-profile_stream-unroll_shadow257_20260919_0913.self`。
核心增量构建 19 秒，前端 20 秒；archive 复制哈希一致：
`42055254bcdaea537cfcb1de363a75bb0d809c7a9f35cb28325d40e704ccc15a`。
SELF SHA-256：`c9f2aea1f767a26004e2e913633d5c379f36dfdb3efb3718772f48ec93cb01fc`。
SPU ELF 保持 `2584eef45e0492ecad8ae7f270d57464ef907332dbc7ec190e7472483fb0c6f7`。
