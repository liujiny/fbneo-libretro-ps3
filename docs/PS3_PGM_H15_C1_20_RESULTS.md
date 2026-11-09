# H15-C1.20 nozoom row cursor

日期：2026-09-20。

C1.19 实机体感与 C1.18 接近，可能稍快；日志无异常。由于 release-no-profile 日志只记录固定
内存生命周期，本次日志与 C1.18 内容哈希相同，不能用于量化 FPS。

C1.20 保留 C1.19 的逐行 packed-color span，并在行内维护当前 packed word 指针和 0..2 phase。
每个非透明 8-pixel block 完成后，根据实际消费颜色数用 11 项常量表推进游标，从热循环移除
`pixel / 3`、`pair - base_pair` 和重复地址重建。colors8 三相展开、mask dispatch、flip、priority
及 framebuffer 写入均未改变；无法取得 row span 时完整回退原路径。

验证：

- 300 万组 packed unpack：PASS；
- worker/profile/shadow 全组合：PASS；
- 100 万组 cursor word/phase 推进等价测试：PASS；
- 仅重编 `pgm_draw.cpp`，core 33.812 秒，frontend 16.930 秒；
- archive 对象一致：`0eecbc952e93dd40a8c8bb5556fb7a5074a739731344e5326fb30329106d2e3a`；
- 两份 archive 一致：`1dc1c2e965fbc37cb5441a3fc48052de08079d727a44af495c3ed208492721cb`。

C1.20 需与 C1.19 同场景实机比较，并检查 sprite 颜色、透明和 flip。
