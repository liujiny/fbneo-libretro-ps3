# H15-C1.14 PPU-only 实机结果与 C1.15 raster split

日期：2026-09-20。

## C1.14 实机结果

输入 `fbneo-ps3-memory.log`，SHA-256：
`ae3c696299d865272e0e0df7fca68730050a18171341ee12b4ca286e08ab2094`。
30 个完整窗口，共 3,600 帧。所有窗口均输出 `spu_worker=0`，`color_spu_samples=0`，
日志中没有 SPU worker/selftest/STATS，确认细粒度颜色 SPU 完全未运行。

| 全部窗口均值 | C1.13 SPU | C1.14 PPU-only | 变化 |
| --- | ---: | ---: | ---: |
| retro_run | 18.656 ms | 18.954 ms | +1.60% |
| sprite draw | 4.128 ms | 2.855 ms | -30.84% |
| color expand | 2.015 ms | 0.902 ms | -55.23% |
| sprites/frame | 23.111 | 23.567 | +1.97% |

高 sprite 子集：C1.13 18.003 ms/frame、3.698 ms sprite；C1.14 18.737 ms/frame、
2.774 ms sprite。sprite/颜色热路径改善非常明确，但总帧均值未同步改善。两次演示并非锁步，
cache I/O、frontend 等其他阶段可掩盖约 1 ms CPU 差异，因此不能从总帧反向否定热路径结果。

C1.13 样本已显示 SPU hit 单次成本高于 PPU fallback；C1.14 又直接证明移除 pipeline 可把
颜色耗时减半。因此继续保留 PPU-only 作为性能方向。多 SPU 会增加当前已经负收益的队列、
同步和复制，不再用于这种 8-color 细粒度任务。GPU 也不优先处理不足 1 ms 的颜色解包。

## C1.15 修改

C1.14 的 sprite draw 约 2.86 ms，其中 color expand 约 0.90 ms，剩余约 2 ms 尚未可靠归因。
C1.15 保持 PPU-only 和全部渲染逻辑不变，增加逐 sprite 的精确路径累计：

- `sprite_nozoom_avg_us`、`sprite_zoom_avg_us`：120 帧窗口内两类完整函数耗时/帧；
- `sprite_nozoom_count`、`sprite_zoom_count`：窗口内真实调用数；
- nozoom 计时覆盖完整 `pgm_draw_sprite_nozoom`；
- zoom 计时覆盖 `pgm_prepare_sprite` 加缩放 raster；
- 每个非空 sprite 只增加起止两次用户态 time-base 读取，不使用 syscall；
- 原 1/16 `sprite_raster_sampled_avg_us` 暂留用于历史对照，但不作为主要判据。

这些字段用于判断下一项大粒度优化：nozoom 占主导时优化 mask/raster 写入；zoom 占主导时
优化 prepare + draw_sprite_line；只有存在可独立、足够大的 raster 任务时才重新考虑 SPU/GPU。

## 验证与构建

全部测试和编译在 `ps3dev-gcc13:rsxfix` Docker 中完成。

- worker/profile/shadow 五种路由组合通过；PPU unpack 随机 oracle 3,000,000 组通过；
- C1.15 继续用 `PS3_PGM_SPU_WORKER=0`；undefined-symbol 检查确认 draw/run 对象没有
  `ps3_pgm_spu_*` 引用；
- 公共 profiler header 使相关对象增量重编：核心 26 秒，前端 17 秒，总计 43 秒；
- `pgm_draw.o`、`ps3_memory_pool.o`、`libretro.o` 与 archive 提取一致，archive 副本一致；
- 没有 clean；编译只有此前已有告警。

测试日志：`H15-C1.15-tests.log`。
构建日志：`H15-C1.15-core-build.log`、`H15-C1.15-retroarch-build.log`。

- `pgm_draw.o`：`6447a1161b8e18d725db8baf869dea7f8fa6a042685b90676aeb941ddbec0893`
- `ps3_memory_pool.o`：`83350e941fa67c1018340105f79132c3e22829395e9e229b0bdc0a4086f687f5`
- `libretro.o`：`8b74df43d6b6fa140f0f9320efb4ddbe37235caa2358b6863da9c5455f1bdaa3`
- archive：`f9a1eb776a0b13757e12db82d45d58614fc0ef0e2e78482766702906b783ac85`
- SELF：`e8ab2f6c74ec737e4898c4b3e91926fc40f5de723826bfc64b4f3c25c35f7c7b`

产物：`RetroArch_FBNeo_PGM_PPU_v1-H15-C1.15_raster-split_20260920_0825.self`。

## 实机判定

使用相同场景约一分钟。确认 `spu_worker=0`、SPU samples 为零，并检查 nozoom/zoom 两类
count 之和与 sprites 数量大致匹配（wide=0 的 sprite 会在分类前返回）。两类耗时之和应接近
sprite draw，但外层遍历、字段解析和空 sprite 会造成合理差值。根据占比选择下一项优化。
