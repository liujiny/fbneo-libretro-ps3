# H15-C1.17 实机结果与 C1.18 release-no-profile

日期：2026-09-20。C1.17 输入日志 SHA256：
`934272aab0b797e93b74fc9f9b5ce94493a1eb31b082e45771fc1d8219a81ab5`。

## C1.17 结果

C1.17 共 35 个窗口、4200 帧。相对 C1.16：

- 全部窗口 nozoom 2.706 -> 2.533 ms，但 frame 18.789 -> 19.713 ms；
- sprite >= 20 窗口 nozoom 2.522 -> 2.227 ms，但 frame 18.442 -> 18.863 ms；
- 用户实机体感更慢。

opaque-run 虽降低局部 nozoom 计时，但未转化成整帧收益，判定失败并从源码完整回退。

## C1.18 修改

C1.18 恢复 C1.16 raster，保持 PPU-only，并使用 `PS3_PGM_PERF_PROFILE=0` 构建 release：

- 移除每 sprite/nozoom/zoom 的 time-base 计时；
- 移除 colors8 抽样计时；
- 移除高频 mask/color cache hit、way、eviction 统计更新；
- 不改变 cache miss、文件读取和渲染逻辑。

这是基于已完成归因后的纯诊断开销回收。该版本不会输出 `[PS3 PGM PERF]` 窗口，评价方式为
同场景肉眼流畅度或 RetroArch 外部 FPS 显示。

## 验证

- Docker 内 300 万组 unpack 测试通过；
- worker/profile/shadow 组合测试通过；
- 显式重编所有受 profile 宏影响的对象；
- `pgm_draw.o`、`pgm_run.o`、`libretro.o` 无未解析 `ps3_perf*` 引用；
- core 32.932 秒，frontend 19.886 秒；
- archive 内 `pgm_draw.o` 与工作对象一致：
  `e032b676517b75e9f9f4a61a10d125fca8bb404dafd652584ed90ede6592c49f`；
- 两份 archive 一致：
  `d0dd8e61093a1c67b77631df6ab5a8c6e656f053ccc868b3201501da58beb78b`。
