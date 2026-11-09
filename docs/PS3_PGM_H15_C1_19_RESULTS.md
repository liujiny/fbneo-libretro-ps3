# H15-C1.18 结果与 C1.19 nozoom row-span

日期：2026-09-20。

C1.18 实机确认比带 profiler 的版本稍快，但肉眼差异较小。日志 SHA256：
`e75b088529d1f6d0d5166c76a993d96bc16c4f64a9312e5b180765d213fae218`；无内存/native
分配失败，无异常退出。C1.18 release-no-profile 保留为当前基线。

## C1.19

保持 C1.18 的 PPU-only、profile=0 和原 raster 写入逻辑。对完全可见的 nozoom 行预取一个覆盖
该行最大颜色消耗量的 packed-color span。成功时，行内所有非透明 8-pixel block 直接从该 span
调用现有三相展开 decoder，避免每块重复执行 color-cache page/tag/way/victim lookup。

与失败的 C1.17 不同，C1.19 不合并像素循环、不使用通用逐像素 decoder、不改变 mask dispatch
或 framebuffer 写入。cache 页边界、ROM wrap、clipped 行自动回退原 colors8 路径；profile 构建也
保持原采样路径。

验证：

- 300 万组 packed unpack 测试通过；
- worker/profile/shadow 全组合测试通过；
- 20 万组 row-span 地址和 phase 等价测试通过；
- 仅重编 `pgm_draw.cpp`，core 33.003 秒，frontend 16.853 秒；
- archive 对象一致：`ecebca9d1b7c86fec172c8deee2aedc2cb5301b8b6d035796c03ddc65642171b`；
- 两份 archive 一致：`7bb6bbb59dc74ca193816133c8d584c4961c7c2331ef5358c37b75fa3bd08076`。

C1.19 仍是 release-no-profile，需要以同场景实机流畅度/FPS 与 C1.18 比较，并检查 sprite 画面。
