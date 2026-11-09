# 提示词：接手 PS3 FBNeo CV1000 移植的性能工作

你接手一个 PS3 上的 FBNeo CV1000 移植项目。仓库在 `/home/humor/src/fbneo-libretro-ps3`
（WSL Ubuntu-24.04），主机 `192.168.2.230`（title `SSNE10001`）。

**先读这两份文档再动手：**

- `docs/HANDOFF_next_model.md` —— 完整交接：环境、代码地图、性能数据、已排除方向、测量方法
- `docs/ps3_release_and_deploy.md` —— 构建/部署/测试配方、踩坑清单

---

## 目前状态

已经完成了交接文档 6.1（页表式地址翻译）和 6.2（12 槽寄存器扩容），
实测 **21.47 → 21.74~22.15 FPS（+1.2~3%）**。

你正准备继续做 6.3 块链接，但根据上游 Salvia 的实测（派发减少 22-25%，帧率 0%，已 revert）
和我们自己的采样数据（负载均匀分散、无热点），**块链接确认排除，不要做**。

你提议的三项（NEGC 原生、延迟槽扩容、idle skip、blitter 批次合并）中，
**暂缓延迟槽和 NEGC**，理由见下。

---

## 第一步：验证前两项是否真的生效（约 30 分钟）

在继续任何微优化之前，先确认已完成的两项**在生成代码层面**确实起了作用。
用带剖析的构建：

```bash
DEF="-D__PS3__ -D__LIBRETRO__ -DWORDS_BIGENDIAN -DSH3_PPC_DRC_TEST=1 \
-DSH3_PPC_CACHE_BYTES=(16*1024*1024) -DSALVIA_CV1K_STAMP_PROBE=1 \
-DSH3_PROFILE_SAMPLE=1 \
-DFRONTEND_SUPPORTS_RGB565=1 -DFBNEO_DEFAULT_AUDIO_RATE=44100 \
-DPS3_MEMORY_DIAGNOSTIC=1 -DPS3_MEMORY_POOL_SIZE_MB=8 -DPS3_NATIVE_MEMORY=1 \
-DPS3_PGM_MASK_FILE_CACHE=1 -DPS3_PGM_COLOR_FILE_CACHE=1"
```

跑重场景基准（**所有比较必须用这个窗口**）：

```bash
python3 tools/ps3_autotest.py --game ddpdfk --frames 2700 --wait 300 \
    --warmup 2600 --start-frame 300
```

然后取 `/dev_hdd0/tmp/fbneo-drc.log` 里的 `genwords` 行，与**基线**对比：

```
基线（16,604 块平均，2700 帧）：
  genwords ... avg 0:25 1:38 2:10 3:24 4:5 5:30     合计约 132 条/块

  0 = body          12 槽扩容应降低它
  1 = memaddr       地址翻译，页表翻译应显著降低它
  2 = memguard
  3 = memaccess
  4 = complete
  5 = exit
```

**判读：**

- `memaddr(1)` 没降 → 页表没被真正走通
  （检查 `drc_read_table` / `drc_write_table` 的填充与哨兵值，以及映射变化时是否重建）
- `body(0)` / 状态写穿透没降 → 12 槽没被用上
  （检查 `reg()` 是否真能分配到 r14-r19，块内是否有代码把它们当临时寄存器覆盖）
- 两项都降但 FPS 没动 → 瓶颈在访存延迟，微优化到头了，见下面「决策树」

---

## 两个必须补上的正确性问题

### （1）你的画面回归方法不成立

报告写「帧 2600：4611 色 vs 基准 4889 色」却说「逐字节比对通过」——**这是矛盾的**。

原因是自动投币是**重试式**的，不同运行的落币时机不同，帧号不对应同一游戏状态，
**跨运行比较颜色数没有意义**。

正确的回归方法（任选其一）：

- 同一次运行内导出同一帧的 BMP 哈希，与基线构建在**同一运行条件**下对比
  （固定 `--coin-frame`、固定输入序列）；
- 或用 savestate：在固定帧存档，两个构建从同一存档恢复后对比后续帧；
- 最低限度：连续跑 3 次同一配置，确认颜色数在**同一构建内**稳定，再谈跨构建比较。

**在方法修好之前，任何「零画质回退」的结论都不作数。**

### （2）12 槽扩容有 ABI 风险

`r14-r19` 是 PPC 非易失寄存器，C 调用者（分派器 `sh3_ppc_call`）期望它们被保存。请确认：

- 块入口保存、**所有**退出路径（包括 guard 退出跳到共享 `emit_tail()` 的那些）都恢复了它们，
  不只是正常返回；
- 跑 **10,000+ 帧 soak**，并换 2-3 个 CV1000 游戏（`mushisam`、`ibara`、`espgal2`）各跑一遍，
  看有无偶发花屏/死机。

**2 帧的比对不足以保证 ABI 正确。**

---

## 关于你提议的三项，用实测数据回应

| 提议 | 我们的实测证据 | 建议 |
| --- | --- | --- |
| 延迟槽扩容（3-5%） | `endreason` 直方图（16,604 块）：`END_BRANCH_DELAYED=0`、`ELIGIBLE=104`、`INELIGIBLE=7`，合计 <1% | **暂缓**：如果真有大收益，这个值不可能是 0 |
| NEGC 原生 | `sh3_drc_ppc.h` 已有 `case 0x600a` 的内联 NEGC 实现 | **先确认是否已做**，可能无剩余收益 |
| Idle skip | 采样剖析无热点；`skip_delay` 仅约 2,900/帧 | 收益小，除非只针对轻场景 |
| Blitter 批次合并 | blitter 本体 **0.6ms/帧** | 上限 1.3%；但可改善**卡顿尖峰**（max 124ms），属体验优化而非帧率 |

---

## 决策树

1. **若 `memaddr` / `body` 没降** → 先修好 6.1 / 6.2 的实现，可能还有 1-3% 没拿到。
2. **若降了但 FPS 没动** → 瓶颈在**访存延迟**（PPE 顺序执行 + 依赖链）。此时微优化已到头，可选：
   - 接受现状（21-22 FPS / 轻场景 43 FPS，画面正确）；
   - 或评估换执行模型（静态重编译估计仅 1.2-1.4x，需数周）；
   - **不要**再投入 SPE 卸载（实测最高 2%）。
3. 只有在 1、2 都解释清楚之后，才考虑下一个微优化。

---

## 硬性规则

- **不要回退画质修复**：显存预分配（`Sh3DrcPreallocate` 先于 VRAM）、16 位 VRAM（1:5:5:5）、
  未分配行读零。
- **不要用背靠背 mftb 做细分计时**：会返回相同值，亚毫秒 span 一律测出 0。
  只能用 `SH3_PROFILE_SAMPLE` 的采样剖析（每 64 块一次）。
- **不要复用构建目录**：容器产物是 root 所有，`ar` / `cp` 会 Permission denied。
  每次用新的 `build-cellsdk-<tag>`。
- 每次改动后更新 `docs/HANDOFF_next_model.md` 第 5 节（排除清单）和
  `docs/ps3_release_and_deploy.md`。
- 主机现在由你独占；上一任不会再碰。

---

## 附：基线速查

| 指标 | 基线值 |
| --- | --- |
| 重场景帧率 | 21.47 FPS / 46.58 ms（起手前） |
| 轻场景帧率 | 43.8 FPS |
| 每帧翻译块数 | 47,190 |
| 每块发射指令 | 约 132 条（body 25 / memaddr 38 / memguard 10 / memaccess 24 / complete 5 / exit 30） |
| 块退出原因 | 间接跳转 50% / 长度上限 33% / 相对跳转 16% / 其他 1% |
| 帧时间构成 | DrvFrame 36ms、blitter 0.6ms、重绘 1.1ms、声音 0.13ms |
| 显存 | 16 位 1:5:5:5，128 个 32 行 slab = 64 MiB |
| 基准窗口 | `--frames 2700 --wait 300 --warmup 2600 --start-frame 300` |