# H15-C1.9 实机结果与 C1.10 partial unpack

日期：2026-09-19。

## C1.9 实机结果

输入 `fbneo-ps3-memory.log`，SHA-256：
`094bfff07ba6f3b6df793b6c243a491e27a47644f815aa4827d906953b7bb188`。
30 个 120 帧窗口，共 3,600 帧；576 条统计全部满足任务、结果、shadow 和提交来源守恒。
69,223 次 sampled shadow 比较无差异，submit=done=1,024,194，lag_max=3。
`mask_early_submit=0`，确认提前提交已撤销；mask 提示覆盖 28.741%，裁剪候选 2,018,091 items。

各版本均取前 24 个完整窗口：

| 平均耗时 | C1.7 | C1.8 | C1.9 |
| --- | ---: | ---: | ---: |
| retro_run | 18.965 ms | 19.237 ms | 19.209 ms |
| sprite draw | 4.269 ms | 4.456 ms | 4.466 ms |
| 已记录 cache I/O | 1.126 ms | 0.990 ms | 1.322 ms |
| sprites/frame | 23.042 | 22.458 | 22.667 |

场景/I/O 不锁步，不能把小差异严格归因于代码；C1.8/C1.9 连续两次都未优于 C1.7，
而且只撤销提前提交没有恢复 sprite draw。因此放弃 mask horizon 扫描和限长，恢复 C1.7 调度。

## C1.10 修改

C1.10 完全移除 C1.8/C1.9 的 horizon helper、扫描、倒计数、调度限长和四个 mask 统计字段。
恢复 C1.7 的 gate12、fresh batch 15/32/48、lead 1/2/3、chain 16/32/40、tail8。

新的优化针对部分透明 mask 的 PPU fallback：

- 全不透明块仍走固定 8-color 展开 helper；
- 部分透明块把 `sprmsktab[mask]`（1–7）传给颜色读取；
- SPU 命中仍返回完整 8 colors，sampled shadow 仍比较完整 8 colors；
- PPU fallback 只检查实际末端、请求实际需要的 packed span，并只解包 1–7 colors；
- 跨 64 KiB 页的 pair-read fallback 和非 packed scalar fallback 同样只处理实际 count；
- clipped raster 已经计算 `color_count`，也复用相同路径。

像素写入仍由原有 256 个专用 nozoom mask 函数完成；颜色顺序、palette、priority、flip、
透明语义和 aoffset 增量不变。SPU ABI、SPU 程序、worker、DMA、mailbox、fence、RSX 未修改。

## 验证和构建

全部测试和编译在 `ps3dev-gcc13:rsxfix` Docker 中完成。

- 部分解包：3 phases × counts 1–7，连续 span 与 pair-read fallback；逐项对比独立 scalar
  结果，并确认 count 之外的输出 sentinel 不被写入。
- 原固定 8 unpack 随机 oracle 3,000,000 组通过。
- stream producer ASan/UBSan 差分 441,850 组通过。
- 五种 worker/profile/shadow 配置通过；interval257 路由仍正确。
- 核心只重编 `pgm_draw.cpp`，22 秒；RetroArch 17 秒；总计 39 秒。没有 clean。
- 对象和 archive 提取一致，archive 副本一致；SPU ELF/header/worker.o 前后哈希未变。
- 只有此前已有的 unused-variable 和 `.bld` format-overflow 告警。

测试日志：`H15-C1.10-tests.log`。
构建日志：`H15-C1.10-core-build.log`、`H15-C1.10-retroarch-build.log`。

- `pgm_draw.o`：`3cf67c852b6db87bf3d8b3bf5653ba69a65245dcad488cb65172408b069ea344`
- archive：`9ee4e5eb24cf6cbc220600b998554dfdec37e9b239f5761bb82bdf1022282d59`
- SELF：`9719d8e7599684afa4d7312d8261818e907e5ac02850a15a9aabf334dff706d8`

产物：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.10_partial-unpack_shadow257_20260919_1417.self`。

## 实机判定

这是 PPU/raster 热路径修改，需要真实 FPS 测量。使用相同设置、场景和时长保存日志。
预期 pipeline 统计回到接近 C1.7；是否降低 sprite draw 取决于部分透明块占比和 SPU miss 比例。
sampled shadow 必须继续为零，任务计数必须守恒。
