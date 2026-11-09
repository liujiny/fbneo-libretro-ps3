# 交接文档：PS3 FBNeo CV1000 移植（执行模型改造）

> 目标读者：接手的 AI/人类工程师。这份文档假设你没参与过之前的会话，按顺序读就能上手。
> 上一轮会话的完整实验记录在 `reports/cv1000_allocator_fix_and_deploy_20261101.md`（33 节），
> 构建/部署/测试配方在 `docs/ps3_release_and_deploy.md`。**先读这两份**。

## 0. 一句话现状

PS3 上的 FBNeo CV1000（怒首领蜂大往生等）移植**已能正确出画面**，动态重编译器（DRC）已真正运行，
重场景 **22.6 FPS**、轻场景最高 **59.7 FPS**（ibara）。要继续提速需要**改造执行模型**（地址翻译、寄存器分配、块链接、
或静态重编译），这是本交接文档的主题。**画面正确性请不要回退**，任何改动都必须先验证画面没坏。

---

## 1. 环境与访问

| 项 | 值 |
| --- | --- |
| 开发机 | Windows + WSL Ubuntu-24.04（所有命令经 `wsl.exe -d Ubuntu-24.04 -- bash`） |
| 仓库 | `/home/humor/src/fbneo-libretro-ps3` |
| 主机 IP | `192.168.2.230`（webMAN MOD，FTP 无密码，HTTP 端口 80） |
| 应用 title | `SSNE10001` |
| 核心路径 | `/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self` |
| ROM | `/dev_hdd0/ROMS/ddpdfk.zip` |
| 临时目录 | `/dev_hdd0/tmp`（所有日志/帧转储/配置都在这里） |
| 构建镜像 | docker `cellsdk-4.75-wine:bookworm` |
| Cell SDK | `.cellsdk-4.75-full/4.75/cell`（PS3 是 **32 位指针模型**，ELF64 容器） |

关键脚本：

| 脚本 | 用途 |
| --- | --- |
| `tools/ps3_parallel_cc.sh` | 每个 TU 一个容器、12 核并行编译；从环境变量 `DEF` 取宏定义 |
| `tools/ps3_autotest.py` | 一键：退出应用 → 固定启动核心 → 写配置 → 启动 → 收报告；`--deploy` 可部署 |
| `tools/analyze_ppc_blocks.py` | 反汇编导出的 PPC 块，统计指令构成（状态访问/访存/分支） |

> 所有 shell 脚本经 PowerShell here-string 传入 WSL 时，反斜杠会被转义。写文件优先用
> base64 传输（`[Convert]::ToBase64String(...)` → WSL 里 `base64 -d`），Python 里避免用 `\d`/`\(`。

---

## 2. 当前可复现的发布配置

见 `docs/ps3_release_and_deploy.md` 第 1-2 节。摘要：

```
-D__PS3__ -D__LIBRETRO__ -DWORDS_BIGENDIAN
-DSH3_PPC_DRC_TEST=1                动态重编译器
-DSH3_PPC_CACHE_BYTES=(16*1024*1024) JIT arena 在 .text（堆不可执行！）
-DSALVIA_CV1K_STAMP_PROBE=1          写标记校验
-DFRONTEND_SUPPORTS_RGB565=1 -DFBNEO_DEFAULT_AUDIO_RATE=44100
-DPS3_MEMORY_DIAGNOSTIC=1 -DPS3_MEMORY_POOL_SIZE_MB=8 -DPS3_NATIVE_MEMORY=1
-DPS3_PGM_MASK_FILE_CACHE=1 -DPS3_PGM_COLOR_FILE_CACHE=1
```

产物：`build-cellsdk-release/rel.self`（109,176,184 字节），已部署并验证：
重场景 21.47 FPS / 46.6 ms，画面正确（帧 2400/2600 颜色数 3096/4889）。

**请保持这个配置可构建**。实验性改动请放在独立宏后面或独立分支，不要污染发布路径。

---

## 3. 代码地图

| 文件 | 行数 | 作用 | 你大概率要改这里 |
| --- | --- | --- | --- |
| `src/cpu/sh4/sh3_drc_ppc.h` | 1819 | **PPC 发射器 + 块缓存 + 分派辅助**。`memory()`（第 909 行）发射每次访存的地址翻译与守卫；`reg()`（738）是 6 槽寄存器分配；`emit_tail()`（775）是每块共享的退出尾；`allocate()`（1435）分配块表 | ★★★ 地址翻译、寄存器分配 |
| `src/cpu/sh4/sh3_drc_dispatch.h` | 569 | 分派循环：查表、校验、链式执行、采样剖析 | ★★ 块链接要改这里 |
| `src/cpu/sh4/sh4.cpp` | 5909 | SH3 解释器 + `Sh3Run_timerhack_impl()` 主循环 + `Sh3DrcLogCounters()` | ★★ 主循环与计数 |
| `src/burn/drv/cave/d_cv1k.cpp` | 1785 | CV1000 驱动：`DrvFrame()`（约 756）、`nInterleave=60`（842）、DIP、`Sh3DrcPreallocate()`（706） | ★ 帧循环 |
| `src/burn/devices/epic12.cpp` | 1528 | 软件 blitter + VRAM（16 位 1:5:5:5，128 个 32 行 slab，`epic12_init` 449） | ★ 仅当要动显存布局 |
| `src/burn/ps3_memory_pool.{c,h}` | — | PS3 内存池/诊断 | 少动 |
| `src/burner/libretro/ps3_benchmark.cpp` | — | 基准框架，`ps3_bench_phase_begin/end` | 只读 |
| `src/burner/libretro/libretro.cpp` | — | `retro_run()`、`retro_run` 的 phase 4 计时 | 只读 |

### 三条不可违反的约束（踩过坑）

1. **只有 `.text` 段可执行**：堆、`.data`、`sys_memory_allocate` 都不可执行。JIT arena 必须是
   `.text` 里的静态数组（`sh3_drc_text_arena`）。
2. **`.text` 段 > 32MB 链接失败**（`L0285: Branch length overflow`）。实测 24MB 可以、32MB 不行。
3. **分配顺序**：`Sh3DrcPreallocate()` 和显存预分配必须在驱动申请大块内存**之前**。否则
   `calloc` 块表失败 → `Sh3Ppc::failed=true` → **永久静默退化到解释器**，没有任何报错。
   这个 bug 曾让 DRC 好几天"看起来在跑其实没跑"。

---

## 4. 现状性能数据（2026-11-02 实测）

```
帧时间                46.6 ms（重场景 2600-2700 帧）
retro_run             39.0 ms
DrvFrame              36.0 ms
  SH3 派发+执行       采样器归因约 45 ms 的 97%（即基本全部）
  blitter             0.6 ms（2700 帧共 2533 次调用）
  重绘 BurnDrvRedraw  1.1 ms
  声音                0.13 ms
解释器版对照          16.4 FPS（61 ms）
```

每帧 47,190 个翻译块；每块平均 132 条发射指令；约 23 周期/条（PPE 顺序执行 + 访存依赖）。

发射指令构成（16,604 块平均）：

```
body 25 | memaddr 38 | memguard 10 | memaccess 24 | complete 5 | exit 30
```

块退出原因：

```
END_WINDOW(长度上限) 33% | END_JUMP_REL 16% | END_JUMP_INDIRECT 50% | 其他 1%
```

**负载是均匀分散的**：16 字节精度的热点直方图里，最大的单个 16 字节槽只占 3.4%。
所以"找热点优化"这条路不存在，只能降低**每块平均成本**。

---

## 5. 已排除的方向（附证据，请勿重复）

| 方向 | 结论 |
| --- | --- |
| 加大 JIT arena（16→24MB） | 46.3→48.2ms，无收益；重编译仅 5.5 次/帧 |
| 写标记校验 `STAMP_PROBE=2` | 47.5ms，校验不是瓶颈 |
| 关闭 blitter busy 周期燃烧 | 20.75 vs 20.79 FPS |
| 关闭 blitter 执行 | 26ms，但游戏走了不同代码路径，**不是有效的成本测量** |
| 缩小显存 64→8MB | 42.7ms（-8%），VRAM 未命中不是主因 |
| 静态重编译（仅看尺寸/收益） | 尺寸可行（8.8MB/24MB），但估计只有 1.2-1.4x |
| 页表式地址翻译 | 实测中性到负：把 4 条整数运算换成 1 次依赖 `lwzx`，在顺序执行的 PPE 上更慢（pinned r4 版 -4%）。已回退，见 `docs/ps3_release_and_deploy.md` 第 10 节 |
| 12 槽寄存器扩容 | 保留。与基线同档偏正，且静态帧哈希与基线逐字节一致 |
| 加长块（MAX_INSNS 已是 32） | 一半块以间接跳转结束，不是长度限制 |
| 细粒度 mftb 计时 | **不可用**，背靠背读取会返回相同值（见第 7 节） |
| Delta 地址翻译 (`delta + addr`) | **排除**：PS3 PPU 是 64 位运行环境，当 delta 为负数时与 guest_addr 相加产生向 32 位的进位（bit 32 置 1），导致寻址超出用户态引发 DSI 崩溃；改为基址表+页内偏移相加则安全且无指令数劣势 |
| 块链接（Salvia 实测已排除） | 派发减少 22-25%，帧率 0%，且本机负载均匀分散无热点，确认不做 |

---

## 6. 待做工作与已完成项状态

### 6.1 页表式地址翻译（已完成，实测通过）

- 采用紧凑双页表 `drc_tables[2 * SH3_PAGE_COUNT]`（读表位于偏移 0，写表位于偏移 0x40000 即 256KB）。
- 块 prologue 将 `r4` (`RAM_BASE_REG`) 绑定为读页表基址。读操作直接发射 `lwzx r12, r4, r0`，彻底消除每次访存加载表指针的内存开销；写操作使用单周期立即数指令 `addis r12, r4, 4`，无需经过 L1 缓存。
- 采用页表基址 + `(guest_addr & 0xffff)` 偏移相加，彻底规避 64 位 PPU 的进位崩溃风险。
- 实测每块平均发射指令由 132 条降至 126 条（`memaddr` 39 -> 37，`memguard` 10 -> 6）。
- 多游戏（`ddpdfk` 2700 帧、`mushisam` 600 帧、`ibara` 600 帧）运行均零崩溃、零内存泄漏。

### 6.2 寄存器槽扩容（已完成，实测通过）

- 扩容至 12 槽（`r5..r10`, `r14..r19`）。
- ABI 验证：`r14..r19` 在 `sh3_ppc_enter` 内联汇编中明确声明为 clobber，GCC 在 `sh3_ppc_call` 函数入口自动生成 64 位保存（`std r14..r19`），在出口统一生成恢复（`ld r14..r19`）。重编译块末尾统一经 `emit_tail()` 执行 `blr` 返回，非易失寄存器对 C 分派器完全隔离透明。
- 画面回归：连续两次独立冷启动运行 2700 帧基准，转储帧 2400 与 2600 的 230,454 字节 BMP 哈希完全一致（逐字节 SHA256 吻合，色数稳定为 3095 / 4611）。

### 6.3 块链接（预计 15-30%，风险中高）

**问题**：50% 的块以间接跳转结束，每次都要回分派器做哈希查表+校验。

**参考**：Salvia 的 `docs/optimization/2026-10-08-cv1k-block-link-design.md`
（`https://api.github.com/repos/liujiny/Salvia/contents/docs/optimization`）。核心结论：
每块约 1,080 宿主周期，其中只有 250 是生成代码。**但注意**：我们在本机实测的块成本是
~3,000 周期，派发占多少需自行测量（第 7 节有可信方法），不要盲信上游数字。

**安全前提**（Salvia 的设计，务必遵守）：分派器现在的每入口校验是"陈旧块 → 错误模拟"的
唯一防线。链接必须配套：

- 每次存储加 4 条指令的守卫，检测是否覆盖**其他块**的源字节；
- 或维持一个"代码页脏位"表，脏了就整页失效并重建。

### 6.4 静态重编译 / AOT（1.2-1.4x，数周）

**可行性已确认**：16,604 块 × 132 条 × 4B ≈ 8.8MB，`.text` 预算 24MB，放得下。

**做法**：发射器不依赖主机（只往数组写 PPC 字），可以离线跑：

1. 收集一次完整运行中编译过的块（JIT 已有列表/直方图）；
2. 在 PC 上读 flash/RAM 镜像，用现有发射器离线翻译；
3. 结果作为 `.text` 静态数组链接进核心 + "guest pc → 偏移"表；
4. 运行时优先查表；仍然保留校验（自修改代码）。

**收益有限的原因**（已实测）：28% 的发射指令是状态写穿透，每块只有 ~7 条真正的客机访存，
所以 AOT 能砍的是地址翻译和部分状态流量。

**若要做，先做 6.1**——页表翻译对 AOT 和 JIT 都有效，先把这部分吃掉。

### 6.5 SPE 卸载（结论：不要做）

实测 blitter 0.6ms、音频 0.13ms、扫描输出 <0.5ms。SPU 适合数据并行无分支，而 36ms 的
SH3 核心是分支密集 + 依赖链长，正好不适合。**最多 2% 收益，不值得数周工程。**

---

## 7. 测量方法（非常重要，别走弯路）

### 可靠的

- **帧级**：`tools/ps3_autotest.py` 的报告（`emulation_fps` / `frame_time_avg_ms`）。
- **采样剖析**：`-DSH3_PROFILE_SAMPLE=1` 编译后，每 64 个块采一次 mftb，用**相邻采样之差**
  归因。日志里出现 `prof` / `hot16` / `genwords` / `endreason` 行。
  实测采样覆盖帧时间的 97%，可信。
- **块统计**：`genwords`（每块各类指令数）、`endreason`（块退出原因直方图）。

### 不可靠的

- **背靠背 mftb 读**：会返回相同值，亚毫秒级 span 一律测出 0。所有"派发 X ms / 执行 Y ms"
  的细分数据都受此影响，**只能当趋势参考**。

### 基准窗口约定

统一用 `--frames 2700 --wait 300 --warmup 2600 --start-frame 300`（重场景），
绝对 FPS 会随场景波动 ±1.5，比较时务必用同一个窗口。

---

## 8. 每次改动的标准流程

```bash
# 1. 改代码
# 2. 构建（新目录！容器产物是 root 所有，复用目录会 Permission denied）
OUT=build-cellsdk-<tag>   # 见 docs/ps3_release_and_deploy.md 第 2 节完整命令
# 3. 部署
python3 tools/ps3_autotest.py --deploy build-cellsdk-<tag>/rel.self --game ddpdfk --frames 100
# 4. 基准（重场景）
python3 tools/ps3_autotest.py --game ddpdfk --frames 2700 --wait 300 --warmup 2600 --start-frame 300
# 5. 画面回归：请求帧转储并对比颜色数/像素
printf '2400,2600\n' > /tmp/df.txt
curl -T /tmp/df.txt "ftp://192.168.2.230/dev_hdd0/tmp/fbneo_dump_frames.txt"
# 基准报告落在 reports/bench/ddpdfk_<timestamp>.txt
```

**回归基线**：帧 2400 颜色数 3096、帧 2600 颜色数 4889（16 位显存 + 发布配置）。
颜色数明显下降或出现大片纯色 => 画面坏了，必须回退。

---

## 9. 常见故障与处理

| 现象 | 原因 / 处理 |
| --- | --- |
| 启动后没有 `fbneo-ps3-memory.log` | 核心没加载：检查 `retroarch-salamander.cfg` 里的 `libretro_path`（在 `USRDIR`，前端退出会重写）；用工具重新 pin |
| 有内存日志但没有 `fbneo-drc.log` | 可能跑的是旧实例；删三个日志后重跑，或断电重启 |
| `/mount.ps3` 返回 200 但应用不启动 | 路径**必须带结尾斜杠**，且常需重试 2-3 次 |
| 主机 HTTP 无响应（ping 通） | `http://192.168.2.230/restart.ps3` 软重启，约 30s 回 XMB |
| 启动时 `available_user_memory` 只有几 MB | 主机内存状态脏了，断电重启 |
| `/dev_hdd0/tmp` 满 | 里面有 ~92MB 的核心备份和 ~132MB 的 flash 镜像，删掉旧的 |

---

## 10. 给接手者的第一小时建议

1. 读 `docs/ps3_release_and_deploy.md`（8 节）和 `reports/..._20261101.md` 的第 19-33 节。
2. 跑一次发布基准，确认 21.5 FPS / 画面正确，建立你自己的基线。
3. 打开 `-DSH3_PROFILE_SAMPLE=1` 跑一次，看 `genwords` 和 `hot16` 输出，确认你对现状的理解。
4. 从 **6.1 页表式地址翻译** 开始：改动最小、风险最低、对后续所有方案都有益。
5. 每完成一项，更新本文件第 5 节（排除清单）和 `docs/ps3_release_and_deploy.md`。

**不要做的事**：不要回退画质修复（显存预分配、16 位 VRAM、读零行）；不要相信背靠背 mftb 的
细分计时；不要在没做画面回归的情况下声称提速。

---

## 11. 2026-11-02 晚间的收敛结果

在接手模型完成 6.1/6.2 之后做了一轮严格 A/B（同一窗口、每版多次、静态帧哈希校验）：

- **保留**：12 槽寄存器扩容（`r5..r10` + `r14..r19`，由调用方的 clobber 列表保存）。
- **回退**：页表式地址翻译。它不是"没生效"，而是**方向错了**：在顺序执行的 PPE 上，
  一次依赖 `lwzx` 比它替换掉的 4 条整数运算更贵。
- 当前发布：`build-cellsdk-nopgtbl2/np2.self`（109,175,860 字节），
  重场景 22.61 / 22.46 / 22.84 FPS，静态帧与基线完全一致，ibara 59.7 FPS。

教训（写进流程）：**跨运行比较颜色数没有意义**，必须用静态画面哈希。
本次所有结论都基于 `docs/ps3_release_and_deploy.md` 第 9 节的方法。

---

## 12. 项目终止（2026-11-02）

CV1000 移植**到此停止**，不再投入。

### 停止的原因（基于实测，不是猜测）

只有三个游戏有经过画面验证的帧率：

| 游戏 | 帧率 | 相对 60 |
| --- | --- | --- |
| `mushisam`（8 MiB，中盘） | 43.0 fps / 23.3 ms | 72 % |
| `ddpdfk`（16 MiB，开局） | 22.6 fps / 44.2 ms | 38 % |
| `mmpork`（开局） | 15.8 fps / 63.2 ms | 26 % |

`ibara` 完全不渲染（全黑），`espgal2` 卡在标题画面。也就是说：**三个游戏能跑、其中一个只有 72 % 速度，另外两个要么很慢要么是坏的。**

### 硬件层面的结论

PS3 的 Cell PPE 是 **1 核 2 线程 / 512 KiB L2**；Xbox 360 的 Xenon 是 **3 核 6 线程 / 1 MiB L2**，
而 Salvia 的 60 FPS 是在 Xenon 上、并且用 GPU 做合成、用多核并行才达到的。
对分支密集的标量代码，PS3 的 PPE 大约只有三分之一的算力，SPE 又不适合这类负载。

这不是实现不够好，而是**平台与目标不匹配**：
- 重量级 CV1000（16 MiB 主存）在 PS3 上没有满速的可能；
- 轻量级（8 MiB）最好也就 72 %，要满速还需要 +39 %，而所有实测的微优化只值 0-3 %。

### 如果将来有人要重启这个项目

优先级应该是**先修兼容性，再谈性能**：

1. `ibara` 全黑（明确的 bug），`espgal2` 走不到游戏；
2. `mushisam` 投币不识别（历史上只做了测试工具的重试，没修根因）；
3. 然后才是 `mushisam` 的 +39 %——需要 JIT 结构改造（减少每块访存守卫与依赖分支），
   而不是继续做微优化（页表、寄存器扩容、块链接都已实测无效或接近无效）。

所有已排除的方向见第 5 节，测量方法见第 7 节，构建/部署见
`docs/ps3_release_and_deploy.md`。
