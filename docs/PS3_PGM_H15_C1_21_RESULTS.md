# H15-C1.21 16 MiB color cache

日期：2026-09-20。

C1.20 实机确认 sprite 颜色、透明和水平翻转正常，体感可能比 C1.19 稍快，但差异很小。
C1.20 保留。

C1.21 将 packed-color file cache 从 128 x 64 KiB（8 MiB）增加到 256 x 64 KiB
（16 MiB），继续使用 2-way set associative。mask cache 保持 4 MiB。依据早期 profile 日志，
高 sprite 窗口每 120 帧仍有约 50–147 次 color page miss，且大部分伴随 eviction；扩大容量
旨在减少同步 64 KiB 文件读取造成的长帧和体感卡顿，而非提高纯 CPU raster 吞吐。

内存预算：此前 native peak 约 58.9 MiB，本版预计增加约 8 MiB。符号检查确认 tag 数组为
0x400 bytes，即 256 个 32-bit tag。

验证：

- 300 万组 packed unpack：PASS；
- worker/profile/shadow 全组合：PASS；
- 依赖驱动增量编译（未 clean），core 33.730 秒，frontend 16.706 秒；
- archive 对象一致：`1ac1b172804b5912eee9f9c564c31a5722699a098aa9d2fb8cf6f474c3b9d300`；
- 两份 archive 一致：`bd92fe282c872fff5abd66492daba8c5279e926d354875214f72814556de6759`。

实机首先确认游戏能加载且 `PGMColorPageCache` 为约 16 MiB、native allocation failure 为零；
随后与 C1.20 比较场景切换和大量 sprite 时的卡顿感。
