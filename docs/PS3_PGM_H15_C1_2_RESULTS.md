# H15-C1.2 PPU 提交准备优化（2026-09-19）

本轮完成日志补全和 PPU batch builder 优化。FPS 收益尚未实机测量。
工作区原有修改保留；没有 clean，没有改 SPU、worker、协议、RSX 或模拟器逻辑。

## 产物

项目根目录下：

- A：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.1_stats_gate12_batch48_lead3_chain40_tail8_20260919_0825.self`
- B：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.2_prep-span_gate12_batch48_lead3_chain40_tail8_20260919_0827.self`

文件名时间来自容器 UTC；对应北京时间 16:25 / 16:27。
当前核心 archive、RetroArch archive、ELF、SELF 都是 B。
原有 0806 版 SELF 保留。仅删除并重新生成了两次链接用的
`retroarch/retroarch_psl1ght.elf`、`retroarch/retroarch_psl1ght.self`。

## 修改与预期影响

运行时代码只改 `src/burn/drv/pgm/pgm_draw.cpp`：

1. A 和 B 都增加 `stream_short_skip=%llu`、`gate12_submit=%llu`。
   前者统计无可用结果、无未完成任务时，被 `<12` 连续块门槛挡下的调用；
   后者统计通过门槛后成功提交的 fresh job，不含 tail chain，也不是恰好在第 12 块提交的次数。
2. B 在一次 batch builder 调用内复用同页 span；跨页重新查询，单个 item 跨页仍按原逻辑停止。
   指针不跨调用保存；准备循环内没有其他缓存访问者。
3. B 将清零长度从整个结构缩为 `16 + 32 * batch_limit`，包含 compact DMA guard。
   即使 ROM/mask/页面边界使实际 count 小于 limit，实际发送前缀及 guard 仍全部初始化。
   当前调用者 limit 最大为 48；协议上限为 64，测试覆盖 0–64。

| 同页 batch 大小 | span 查询：A → B | 清零字节：A → B |
| --- | --- | --- |
| 15 | 15 → 1 | 2064 → 496 |
| 16 | 16 → 1 | 2064 → 528 |
| 32 | 32 → 1 | 2064 → 1040 |
| 40 | 40 → 1 | 2064 → 1296 |
| 48 | 48 → 1 | 2064 → 1552 |

以上是代码路径操作量与差分测试统计，不是 PS3 吞吐或 FPS 测量。
预期减少 PPU 准备 job 时的 cache tag 查询、LRU 更新、profiling 调用和清零写入。
DMA 输入/输出长度、submit/poll/fence、gate12、batch48、lead3、chain40、tail8 全部保持。
shadow compare 仍全量运行。

颜色文件缓存的 accessor hit/way-hit 计数会随查询合并而下降，不能直接用这两个计数的原始值比较收益。
SPU STATS 的 `cache_hit` 是已完成结果缓存命中，语义保持。

## 构建与检查

全部编译（包括差分测试原生编译）均在 `ps3dev-gcc13:rsxfix` 中完成。
Docker image ID：`sha256:1b25dc0487f27eeb33c14e53b6ee5e5948e7b87f7ec1bc3918c75bcb0e28ef4d`。

核心参数：`platform=psl1ght COMMONLV= PS3_MEMORY_DIAGNOSTIC=1 PS3_MEMORY_POOL_SIZE_MB=64 PS3_NATIVE_MEMORY=1 PS3_PGM_MASK_FILE_CACHE=1 PS3_PGM_COLOR_FILE_CACHE=1 PS3_PGM_PERF_PROFILE=1 PS3_PGM_COLOR_CACHE_2WAY=1 PS3_PGM_COLOR_PREFETCH=0 PS3_PGM_SPU_WORKER=1`。
前端参数：`-f Makefile.psl1ght PS3_RSX_HOST_SIZE_MB=4 PS3_MENU_BUFFER=0 PS3_FULL_MENU=0 info all`。
均使用 `-j"$(nproc)"`，由源文件时间戳触发增量构建。

| 版本 | 核心编译及 archive | 前端链接及 SELF | 合计 |
| --- | --- | --- | --- |
| A | 22 秒 | 17 秒 | 39 秒 |
| B | 21 秒 | 16 秒 | 37 秒 |

计时不含容器启动、哈希校验及复制。两次均仅编译 `pgm_draw.cpp`。
日志：`H15-C1.1-stats-{core,retroarch}-build.log`、
`H15-C1.2-prep-span-{core,retroarch}-build.log`。
A 的 Makefile Git 元数据查询遇到容器 ownership 提示，但构建成功。
B 通过容器进程级 Git safe.directory 配置消除提示，没有修改宿主 Git 配置。
两次均有相同的既有 unused-variable 和 `.bld` sprintf 长度警告，本轮未引入新警告。
相对于本轮快照的 whitespace 检查通过；相对 Git HEAD 的全量检查仍报告原有空白问题。

每次均用 `ar p ... pgm_draw.o` 提取并 `cmp` / SHA-256 核对；复制至 RetroArch 后再次核对整个 archive。

| 校验对象 | A SHA-256 | B SHA-256 |
| --- | --- | --- |
| pgm_draw.o / archive 内对象 | `37f62c5faab0fa33e0e17930e2ddcfd29b8197bd7db2566574ca14237544e580` | `7bf3c8caae42468b94fdc8344adca48525d837c102289be2d73fc27c278248b3` |
| 核心 / RetroArch archive | `a1f679d03b7aa1965195460f2ffad08a4a90b40fd47d157d8a2ef0e2923564d8` | `ac50a5f646ec9fd76d0ed587aaaa7eba8185c01eda82c41f158927c31ceac1be` |
| 保存的 SELF | `0900d43bd33a5125a61c0a9a26793789b51797ae87776a9a230b8e89d3b261f4` | `52bf08967dd7b1973cef7707fef5cc376be9dfd42118028b14a45342e3afc52c` |

SPU/worker 前后哈希保持：

- worker.o：`04aaf173b2e14a1a9b47e4e0b6bcd4e840fb65781fee37a18681bd56572f0687`
- SPU generated header：`b1b2d6cd8f421a0ad3b04246c822af09795047a9378e916e0d13bf612fc024f5`
- SPU ELF：`cf313f3904465f6a5a17a100d1db980e7469426848005fbd4b23632f1b5e01f3`

新增 `tools/test_pgm_spu_batch.py`，抽取实际生产 builder 与修改前 builder，使用原协议结构进行差分。
441,850 组通过 ASan/UBSan，覆盖所有 0–64 limit、三种 phase、64 KiB 页边界、ROM/mask 截断、
空批次、RAM/file-cache 模式和随机输入；比较完整发送前缀（含 guard）、换页序列和独立逐像素解码。
56 个 STATS 格式参数数量和 unsigned/unsigned-long-long 类型也通过检查。
日志为 `H15-C1.2-prep-span-test.log`。这不代替 PS3 实机测量。

复现（现有 `.bak_H15C1_1` 的 builder 已与本轮测试基线逐字核对一致）：

```bash
rtk proxy docker run --rm -v "$PWD:/work" -w /work ps3dev-gcc13:rsxfix \
  python3 tools/test_pgm_spu_batch.py src/burn/drv/pgm/pgm_draw.cpp.bak_H15C1_1
```

## 下一次实机采样

现在暂停的原因是需要真实 FPS 测量，符合交接中的测试条件。
同一游戏、同一场景、同一前端配置，缓存预热后 A/B 各跑相近时长，提供 FPS / frame time 和完整 STATS。
用采样前后差值比较 `submit`、`decoded`、`used`、`cache_hit`、`abandoned`、`late_skip`、
`stream_short_skip`、`gate12_submit` 和 fresh/chain 分项；每帧指标需同区间帧数。
仍查看 `shadow_bad=0`、`shadow_pixel_bad=0`；采样时可能存在一个 in-flight job，
故瞬时 `submit-done=1` 不代表丢任务，已排空时才应相等。

交接数据 decoded/submit 约 20 项，used/decoded 约 49%，abandoned/decoded 约 51%。
仅增加 submit 或 batch 上限可能增加无用工作；需要上述新计数与 FPS 后再调 gate/batch。
另有两个后续热点值得单独处理：每次 SPU 命中仍重复 PPU reference 解码；
现有 SPU 命中早返回跳过 `pgm_perf_in_color_block` 复位与 color-expand 采样结束逻辑。
因此本轮优先使用实际 FPS、frame/sprite 总耗时与 STATS，避免把 color-expand 采样值当作完整颜色路径耗时。
