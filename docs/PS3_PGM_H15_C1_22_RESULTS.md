# H15-C1.22 resident packed ROM

日期：2026-09-20。

用户报告 C1.21 平均帧率与前版接近，但运行约一分钟会出现一次卡顿。C1.21 日志确认 16 MiB
color cache 分配成功、native failure 为零。旧 profile 日志显示 cache miss 会在渲染线程直接执行
`fseek` + `fread` 64 KiB，color 和 mask 都可能造成长帧；扩大 color cache 并未消除体感卡顿。

C1.22 关闭 `PS3_PGM_COLOR_FILE_CACHE` 和 `PS3_PGM_MASK_FILE_CACHE`，保留最终解密后的 packed
color ROM（约 34 MiB）及 mask ROM（16 MiB）常驻。运行期 accessor 直接访问内存，不创建 backing
cache 文件，也不会因 sprite 页面 miss 阻塞读取 HDD。C1.20 row-span/cursor、PPU-only 和
release-no-profile 保持不变。

预计相对 C1.21 增加约 31.5 MiB native live memory，PGM 侧约 88 MiB。该版本的首要实机检查是
加载成功、`native_failures=0`，以及持续运行超过一分钟后周期性卡顿是否消失。

验证：

- 300 万组 packed unpack：PASS；
- worker/profile/shadow 全组合：PASS；
- 仅重编 `pgm_run.cpp`，core 5.691 秒，frontend 20.029 秒；
- archive 对象一致：`041de412a90c4d2dd5a317868b3c94a52c823b1f4aca627c556b956e13894bda`；
- 两份 archive 一致：`453ed7c0b0ff63b76e2ecc2a993d7dc9d378db2c45e3e0ab439fd150fdbb21e5`。
