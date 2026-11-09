# H15-C1.16 实机分析与 C1.17 opaque-run

日期：2026-09-20。输入日志：`fbneo-ps3-memory.log`，SHA256
`2c1d36abdd8e4aaf5d17fc14338312a3a6380f6704c9ad62743cbeaf7c4ed068`。

## C1.16 实机结论

共 28 个窗口、3360 帧。全部窗口均值：18.789 ms/frame、2.732 ms sprite draw、
2.706 ms nozoom、0.018 ms zoom、0.904 ms packed-color。sprite >= 20 的 22 个窗口：
18.442 ms/frame、2.555 ms sprite draw、2.522 ms nozoom、0.023 ms zoom。

nozoom 占完整 nozoom+zoom 路径时间 99% 以上，zoom、多 SPU 和 GPU 都不是下一步最大收益点。
历史 mask histogram 为 0x00 约 68.05%、0xff 约 24.46%、其他约 7.49%。

## C1.17 修改

对完全可见、非 flipx、mask span 可用的连续 0x00 block 增加 opaque-run fast path：

- 连续多个 8-pixel block 只做一次 color cache lookup；
- packed 5-bit color 边解包边写 `pTempScreen` 和 `SpritePrio`；
- 不再为每块生成临时 `colors[8]`，也不再逐块调用 colors8；
- color cache 页边界、ROM 尾部/wrap、非 packed、flip 和 clipped 路径全部回退到原实现。

注意：opaque-run 绕过原 colors8 抽样点，因此 C1.17 的 `color_expand_sampled_avg_us` 不可直接与
C1.16 比较；主要判据为 `sprite_nozoom_avg_us`、`sprite_draw_avg_us` 和真实 FPS。

## 验证

- Docker 内 `PASS 3000000 unpack cases`；
- SPU worker/profile/shadow interval 全组合测试通过；
- PPU-only 增量编译，仅重编 `pgm_draw.cpp`；
- core 20.914 秒，frontend 17.109 秒；
- archive 中 `pgm_draw.o` 与工作对象 SHA256 一致：
  `0506b24cfe5cb74155253cba55ed0ae49680bacbcd93544eed406ce8cf1eabc2`；
- 两份 archive SHA256 一致：
  `18a3ad2abe94bf89f7d1e6a785faeb370e78d4975f5efca479baf20078fcb144`。

该修改触及实际 raster 热路径，下一步需要同场景实机检查画面并测量 FPS/nozoom 时间。
