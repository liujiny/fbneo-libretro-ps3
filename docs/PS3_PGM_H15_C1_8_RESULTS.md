# H15-C1.8：mask 引导的提交范围

日期：2026-09-19。

## 输入依据：C1.7 实机日志

输入 `fbneo-ps3-memory.log`，SHA-256：
`4ca55badae302a7f40eecd7f7f908a3c463ece72ad99ca9fe70d482ca1a2cb00`。
24 个 120 帧窗口，共 2,880 帧，463 条 SPU 统计。
56,020 次 shadow 抽样均无差异，任务/结果/抽样/提交来源计数全部守恒。
最后 `submit=done=893247`、`lag_max=3`。

| 平均耗时 | C1.6 前 24 窗口 | C1.7 全部 24 窗口 |
| --- | ---: | ---: |
| retro_run | 19.028 ms | 18.965 ms |
| sprite draw | 4.154 ms | 4.269 ms |
| 已记录 cache I/O | 0.856 ms | 1.126 ms |
| CPU video callback | 0.598 ms | 0.429 ms |

仅有约 0.34% 的整帧耗时差，且场景/I/O 不一致，不能认定 C1.7 有净 FPS 收益。
C1.7 `stream_short_skip/calls=48.714%`、`abandoned/decoded=41.691%`，
说明可以继续调查短流覆盖和无效预测，不能把它们直接换算成耗时。

## C1.8 修改

生产代码仅改 `src/burn/drv/pgm/pgm_draw.cpp`。

无缩放、整行横向可见的路径已取得 maskrow。对当前颜色调用扫描到本行下一个部分透明 mask：

- 全不透明 mask `0x00`：消耗 8 个颜色，下一颜色调用仍是 +8。
- 全透明 mask `0xff`：不调用 color8，不消耗颜色，不打断颜色流。
- 部分透明 mask：本次 color8 仍可服务，但只消耗 1–7 个颜色，之后打断 +8 流。

计算的 horizon 包含当前调用和最后的部分透明调用。每段只扫描一次，通过倒计数复用，
不是每个 color8 都重扫余下 mask。若本行没有部分透明终点，返回 unknown，
不把行尾误判为颜色流结束。maskrow 不可用、裁剪、缩放等路径继续传 unknown。
翻转只影响目的像素顺序，源颜色的消费顺序不变。

调度使用提示：

1. 已知至少还有 16 次连续调用时，允许在历史 sequential_run 尚未达到 gate12 前提交。
2. fresh batch 按 `horizon - lead` 限长；tail chain 按
   `horizon - (cache_remaining_after_current + 1)` 限长，避免跨越已知终点。
3. unknown 仍使用原 gate12、fresh 15/32/48、lead 1/2/3、chain 16/32/40、tail8。

不改解包函数、像素栅格化、SPU ABI、worker、DMA、mailbox、fence、RSX 同步或模拟器核心。
没有增加多 SPU/GPU offload。此步先利用确定的源数据范围改善提交，避免扩大无效预解码。

## 新日志字段

| 字段 | 含义 |
| --- | --- |
| `mask_hint_calls` | 带已知终点提示的 color8 pipeline 调用数 |
| `mask_early_submit` | gate12 之前凭已知长度成功提交的 fresh job 数 |
| `mask_cap_items` | 候选 fresh/chain batch 被提示裁掉的 item 数之和 |
| `mask_no_submit` | 已通过 fresh gate，但提示范围内无可提交 item 的次数 |

`mask_cap_items` 不是实测节省的弃用数：它含未提交的候选量，不能直接计为 FPS 收益。
`gate12_submit` 继续统计通过原历史 gate 的 fresh job；新来源单独统计，满足：
`submit == gate12_submit + mask_early_submit + tail_chain`。
`tools/analyze_pgm_perf.py` 已加入这条检查及 mask 提示覆盖率。

## 静态与本地验证

所有编译都在 `ps3dev-gcc13:rsxfix` Docker 中完成。

- 新增 `tools/test_pgm_spu_horizon.py`：提取生产 horizon helper、限长 helper 和完整 scheduler，
  使用模拟异步 worker。覆盖所有 1–254 部分 mask、长度 0–8 的四类 mask 组合、
  透明块跳过、倒计数、不同长度流、流跳转、0/1/2/3/12/80 调用延迟、提交失败。
- 对每次返回的 SPU 命中按源 pixel 独立检查八个结果，并检查有提示提交不跨越终点。
  每组 1,418,645 次调用，21 条统计记录全部守恒。
- 提示开启：31,184 次提交范围断言通过；模拟 worker 提交 43,196 次，
  decoded=1,068,971，used=835,112。
- 提示关闭的相同合成调用序列：提交 40,596 次，decoded=1,223,981，used=755,462。
  这是可重复的调度模型结果，不是实机吞吐/FPS 测量；没有模拟 cache I/O 或真实 DMA 时间。
- 原 stream producer 差分 ASan/UBSan：441,850 组通过。
- PPU unpack 独立随机 oracle：3,000,000 组通过。
- shadow/router 五种 worker/profile/interval 配置通过；模拟 worker 接口适配新参数。

日志：`H15-C1.8-tests.log`。已有 native 测试不执行真实 SPU 指令；也没有宣称对所有
sprite 像素进行全量整帧差分。生产栅格化代码保持不变，实机保留 shadow257。

## 增量构建与交付

- 核心只重编 `pgm_draw.cpp`，24 秒；RetroArch 19 秒；整个流程 44 秒（含复制/校验）。
- 没有 clean；仅重新生成可重建的 RetroArch ELF/SELF，旧版具名 SELF 保留。
- `pgm_draw.o` 与 archive 提取对象一致；核心 archive 与 RetroArch 副本一致。
- SPU ELF、generated header、worker.o 的构建前后哈希未变。
- 核心仍有原来的六项 unused-variable 和两项 `.bld` format-overflow 告警，未新增。
  工作区此前的行末空白仍保留，没有为本轮进行无关清理。

对象 SHA-256：`e6acdd624917bc0785b4fed8e4d36680b1feec1318795b9a5b4a3b96139c3e03`。
archive SHA-256：`94f8e5f9e6c95522444f19732e3eadfc27b30af43c20fccd5a8d9ce737a4c59b`。
SELF SHA-256：`8e78b50bc7e0a11305303cf33531070f0470e4e4b1f9ff41d56b4061881e8291`。

产物：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.8_mask-horizon_shadow257_20260919_1405.self`。
构建日志：`H15-C1.8-core-build.log`、`H15-C1.8-retroarch-build.log`。

## 实机测量目标

预期减少对已知终点之后的无效工作，并覆盖历史 gate12 无法及时启动的一部分连续流。
额外 mask 扫描、更多短批次提交和信号开销也可能抵消收益，必须测真实帧率。

使用与 C1.7 相同的设置、场景和时长，保存新日志。重点观察整帧、sprite draw、cache I/O、
`mask_hint_calls` 覆盖率、`mask_early_submit`、有效 used/calls、弃用和 late_skip。
仍要求 sampled shadow 两项为零、任务计数守恒。此处暂停是为了 FPS 测量，
不要求重新进行基础 DMA 自检排障。

## C1.8 实机回报

新 `fbneo-ps3-memory.log` SHA-256：
`2aa39d0f0382fce510271a49a5965533c0d010ea660da98ed9af1646d1f35f12`。
30 个完整窗口，共 3,600 帧，588 条统计全部通过守恒检查。
73,910 次抽样比较无差异，最后 submit=done=1,188,458，lag_max=3。

新路径确实生效：mask_hint_calls=10,957,437，占 calls 的 28.435%；
mask_early_submit=51,392；候选 mask_cap_items=2,114,866；mask_no_submit=13,948。
累计 used/calls=49.292%、abandoned/decoded=38.308%、poll_pending/calls=1.992%。

为了避免直接混用不同时长，帧耗时比较只取两版前 24 窗口：

| 平均耗时 | C1.7 | C1.8 |
| --- | ---: | ---: |
| retro_run | 18.965 ms | 19.237 ms |
| sprite draw | 4.269 ms | 4.456 ms |
| 已记录 cache I/O | 1.126 ms | 0.990 ms |
| CPU video callback | 0.429 ms | 0.392 ms |

C1.8 整帧耗时高约 1.44%，sprite draw 高约 4.38%。场景并未锁步，因此不能确认这是
代码造成的退化；但不能宣称本轮已经提高 FPS，C1.8 暂不视为性能胜出版本。

另取两版恰好 calls=30,343,168 的统计记录作调度描述（仍不等于相同场景）：
C1.7 submit=893,247、decoded=24,690,636、used=14,396,907；
C1.8 submit=933,438、decoded=24,276,224、used=15,754,988。
提示策略伴随更多提交、更少预解码和更多有效结果，但这些改善尚未转化为可确认的帧率收益。

后续应把提前提交与终点裁剪拆开做对照，区分额外 mask 扫描/信号提交成本和覆盖收益，
而不是在此基础上立即扩大多 SPU 预取。这里只记录分析，没有回退代码或生成新版本。
