# H15-C1.4 contiguous stream DMA

日期：2026-09-19。基线为 C1.3 shadow257。

## 实现

PGM speculative batch 本来为每 8 像素构造一个 32 字节 item，包括独立 packed span、phase 和未使用字段。
C1.4 利用 batch 像素必然连续这一条件，改为 16 字节 header 加一段连续 packed words：

- header：magic、8-pixel group count、起始 phase、packed byte count；
- SPU 在整个 batch 中连续推进 phase，直接写连续 result buffer；
- stream 命令在 signal 中携带 count 和 phase，SPU 在 DMA 前即可算出输入长度；
- SPU 收到 header 后再次检查 magic、count、phase 和 packed length；
- 旧 item ABI、同步命令和 legacy async decode 保留。

文件缓存模式保持旧边界语义：单个 8-pixel item 若跨 64 KiB 页面则停止 batch；
若页面边界正好位于两个 item 之间，则 PPU 分两段复制到连续 stream buffer。
RAM 模式允许自然跨页。

| batch | C1.3 输入 DMA | C1.4 输入 DMA | 减少 |
| ---: | ---: | ---: | ---: |
| 15 | 496 B | 96 B | 80.6% |
| 16 | 528 B | 112 B | 78.8% |
| 32 | 1040 B | 192 B | 81.5% |
| 40 | 1296 B | 240 B | 81.5% |
| 48 | 1552 B | 272 B | 82.5% |

输出 DMA 不变，仍为 `count * 8` 向 16 字节取整。gate12、batch48、lead3、chain40、tail8、
shadow257 和结果顺序不变。

## 多 SPU 与 GPU 评估

当前 renderer 只有一个有序 inflight job 和一个 completed cache。仅创建第二个 SPU 不会并行；
有效的双 SPU 需要至少两个独立 input/result/done slot、带 slot/token 的 submit API、按像素顺序回收的 reorder queue，
以及 stream break 时对两个任务分别作废。C1.4 先压低单任务 PPU/DMA/SPU 固定成本，实机日志若仍显示大量 pending、
稳定长流和 SPU 延迟，再实现 two-ahead 双 SPU，收益判断会更可靠。

GPU 目前只在 RetroArch RSX driver 中接收完成的 RGB565 framebuffer。core 内没有 RSX context；
完整 sprite GPU 化需跨 core/frontend ABI，并复刻 sprite 顺序、priority、zoom、透明、blend/shadow 和 clipping。
这是独立的大改动。较近的 GPU 方向是优化最终 framebuffer upload，但现有 driver 已使用四纹理轮换和 RSX texture；
应先增加 video callback/upload/flip 分段计时，确认它是否在 `retro_run` 之外形成瓶颈。
当前 PGM 日志明确显示 sprite draw 是数毫秒级，因此先推进 SPU decode/pipeline。

## 构建与验证

所有编译都在 `ps3dev-gcc13:rsxfix` 内完成，没有 clean。因 ABI 变化重编 SPU image、
`pgm_draw.o` 和 `ps3_sprite_worker.o`，其余核心对象沿用缓存；RetroArch 重新链接。

- 核心及 archive：22 秒。
- RetroArch ELF/SELF：16 秒。
- SPU ELF SHA-256：`2f9ffc83118b8fe6954fcb737f43fdbfaf73ccca0990ec8f1ed534659883c0a0`。
- `pgm_draw.o` SHA-256：`47513ff82f44c267718846cb8da817d4d4c956d7d79a722635a1ac3ca167b564`。
- `ps3_sprite_worker.o` SHA-256：`4810f12318a8edd8a888862fc59c237dc29ab1f70c1dca832e0968e77e3df182`。
- core/RetroArch archive SHA-256：`10ae4e3d9782373822ebc5f5d173a3fb4b6820bee399f7eff34113fc5718ac74`。
- SELF SHA-256：`8c9e1ee2e560b119ccad3d9d515af4512f1ebe0b509238452dd01807443a951d`。

`ar p` 提取的两个修改对象均与工作树对象逐字一致；复制前后的 archive 哈希一致。
构建没有 error；warning 为工作区已有 unused code/variable 和 `.bld` sprintf 警告。

`tools/test_pgm_spu_stream.py` 用修改前 builder 作差分，441,850 组 ASan/UBSan 测试通过，
覆盖 count 0–64、三种 phase、RAM/file cache、64 KiB 边界、ROM/mask 截断与随机输入；
逐像素对比旧 item decode、新 stream decode 和 packed ROM oracle。
shadow/router 五种配置测试也全部通过。

日志：`H15-C1.4-stream-test.log`、`H15-C1.4-spu-build.log`、
`H15-C1.4-stream-{core,retroarch}-build.log`、`H15-C1.4-shadow-test.log`。

## 实机暂停点

产物：`RetroArch_FBNeo_PGM_SPU_v1-H15-C1.4_streamDMA_shadow257_gate12_batch48_lead3_chain40_tail8_20260919_0902.self`。

这是 SPU ABI 和 signal command 变化，必须实机检查：worker init、DMA selftest、首次 debug stage、
持续 `submit-done==inflight`、`decoded==used+abandoned+pending`、抽样 `shadow_bad=0`、
`shadow_pixel_bad=0`。同一演示段约 60 秒日志即可同时判断帧耗时与 pending/lag 是否下降。
