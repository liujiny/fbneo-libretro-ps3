# H15-C1.9：保留 mask 裁剪，撤销提前提交

日期：2026-09-19。

## 决策依据

C1.8 实机日志正确性和计数全部通过，mask 提示覆盖 28.435% 的 color8 调用。
在两版都达到 calls=30,343,168 时，C1.8 相对 C1.7：

- submit：893,247 → 933,438（+4.50%）；
- decoded：24,690,636 → 24,276,224（-1.68%）；
- used：14,396,907 → 15,754,988（+9.43%）；
- abandoned/decoded：41.691% → 35.101%。

但前 24 个 120 帧窗口的 retro_run 均值从 18.965 ms 变为 19.237 ms，sprite draw
从 4.269 ms 变为 4.456 ms。场景不锁步，不能证明退化，却也没有性能胜出证据。
因此拆分 C1.8 的两个行为，优先移除会增加提交/DMA/信号次数的提前提交。

## 修改

C1.9 保留：

- maskrow 计算的已知连续颜色 horizon；
- fresh batch 按 `horizon - lead` 限长；
- tail chain 按当前 cache 距离和 horizon 限长；
- C1.7 batch-range、C1.6 PPU unpack、shadow257；
- gate12、batch 15/32/48、lead 1/2/3、chain 16/32/40、tail8。

C1.9 撤销 C1.8 的 gate12 前提交。`mask_early_submit` 字段继续输出以便计数格式兼容，
本版必须始终为零。版本日志为 `SPU V1-H15-C1.9 mask-cap gate12 batch48 lead3`。
SPU ABI、worker、DMA、mailbox、fence、RSX 和像素绘制均未修改。

## 验证

所有测试和编译均在 `ps3dev-gcc13:rsxfix` Docker 中完成。

- scheduler/horizon ASan/UBSan 模拟：每组 1,418,645 次调用；不同延迟、提交失败、
  流跳转、mask 组合、提示范围和任务守恒检查通过。
- 同一合成调用序列，cap-only 相对无提示：submit 40,596 → 37,895（-6.65%），
  decoded 1,223,981 → 981,217（-19.83%），used 755,462 → 760,112（+0.62%）。
  这是调度模型结果，不是 PS3 FPS 或 DMA 时间测量。
- stream producer 441,850 组差分通过；PPU unpack 3,000,000 组随机 oracle 通过；
  五种 shadow/router 配置通过。

测试日志：`H15-C1.9-tests.log`。

## 增量构建

核心只重编 `pgm_draw.cpp`，21 秒；RetroArch 17 秒；总计 38 秒。没有 clean。
`pgm_draw.o` 与 archive 提取对象一致，archive 复制哈希一致。
SPU ELF、generated header、worker.o 哈希在构建前后不变。
编译只有此前已有的 unused-variable 和 `.bld` format-overflow 告警。

- `pgm_draw.o`：`75b518241f469f0e1f54f9090230abbe976b9e040ad12441faede478b4cd8555`
- archive：`714dcd376d5772f8a8d68d23574bdfd79ae7ac0f7bd726ee2b61002d923213d4`
- SELF：`63a6537532e74ec90555eb6ea49c88d5b92d8c7a854867010451267a5fdd8830`

产物：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.9_mask-cap_shadow257_20260919_1411.self`。
构建日志：`H15-C1.9-core-build.log`、`H15-C1.9-retroarch-build.log`。

## 实机判定

使用与 C1.8 相同场景和时长。除常规正确性、整帧和 sprite draw 外，确认：

- `mask_early_submit=0`；
- `submit == gate12_submit + tail_chain`；
- mask 提示仍有覆盖，`mask_cap_items` 非零；
- decoded、abandoned、used、poll_pending 的变化。

如果 C1.9 帧耗时恢复而弃用仍下降，继续优化 horizon 扫描成本；如果仍不优于 C1.7，
则 mask 扫描/短批次本身不划算，应回到 C1.7 基线并转向更大粒度的 raster 或多 SPU 测量。
