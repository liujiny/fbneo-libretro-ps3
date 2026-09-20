# H15-C1.23 resident mask / 16 MiB color cache

日期：2026-09-20。

C1.22 同时常驻 packed color ROM 与 mask ROM，KOV2 因内存不足黑屏，判定不可用。

C1.23 改为混合方案：

- color：恢复 16 MiB、2-way file cache；
- mask：关闭 file cache，16 MiB mask ROM 常驻；
- 保留 C1.20 row-span/cursor、PPU-only、release-no-profile。

相对 C1.21 预计只增加约 12.6 MiB native live memory，约为 67 MiB；消除所有运行期 mask
`fseek/fread`，color miss 仍可能同步读取。若 KOV2 可加载且周期性卡顿改善，说明 mask paging 是主要
来源；若仍约一分钟卡顿，则下一步恢复 C1.21 内存布局并处理 color miss 异步化。

验证：300 万 unpack 与 worker/profile/shadow 组合全部通过；仅重编 `pgm_run.cpp`，core
7.234 秒、frontend 19.995 秒。archive 对象 SHA256
`c65dda57e03098a64690b0febb49e31a6037511e4a98cae8fe46cf8a5067c3aa`，两份 archive 均为
`4954dcbb59240d5cb3004595da9ab6a13f978b78f48c92bb4e2be85ff91daa30`。
