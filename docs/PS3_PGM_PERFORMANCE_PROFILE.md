# PS3 PGM performance profiling

The optional profiler is disabled by default. Enable it in the PS3 core build
with `PS3_PGM_PERF_PROFILE=1`. It emits a two-line summary every 120 emulated
frames, so profiling does not write a log entry for every frame.
When `PS3_MEMORY_DIAGNOSTIC=1` is also enabled, each summary is additionally
appended to `/dev_hdd0/tmp/fbneo-ps3-memory.log` with the
`[PS3 PGM PERF]` prefix.

The summary reports average and maximum `retro_run` frame time, average PGM
sprite draw and zoomed sprite preparation time, sampled sprite rasterization,
average sprites per frame, sampled packed-color expansion time, and audio
post-processing time (DSP plus the frontend callback). Rasterization samples
one of every 16 sprite descriptors. Color expansion samples one of every 64
eight-pixel blocks and one of every 256 direct pixel reads. Both sampled times
are scaled estimates; measured color-cache I/O is subtracted from sampled
color time. Sprite preparation time is wall time and can include cache I/O.

Mask and packed-color cache counters report accessor hits and misses, attempted
seeks, `fread` calls, total bytes, average read size, and time spent in seek/read
operations. They also report pages read before during this game session,
adjacent page reads, and consecutive ascending page reads. A profiler-only
4,096-page bitmap per cache
tracks repeats; pages beyond that range are excluded from repeat counts. Cache
I/O time is also reported separately from frame time; `avg_frame_cpu_est_us`
subtracts measured cache I/O from average frame time. It is an estimate because
it includes the frontend and other work inside `retro_run`. With profiling
enabled the page history uses about 1 KiB; the normal build has none of this
storage.

To compare games, use the same build and frontend settings, warm up each game,
then collect several 120-frame summaries. Record the game, emulator or hardware,
render/audio settings, and whether the caches activated. RPCS3 results should be
reported as RPCS3 measurements; they do not establish real PS3 hardware
performance. Real PS3 hardware validation remains pending.

## Packed-color cache experiments

The file-backed color cache remains 128 pages of 64 KiB (8 MiB total). The
default `PS3_PGM_COLOR_CACHE_2WAY=1` layout is 64 sets by two ways with one
replacement bit per set; set it to `0` to restore the original 128-entry
direct-mapped index. The profiler reports way hits, evictions, and conflict
replacements.

`PS3_PGM_COLOR_PREFETCH=1` enables PSL1GHT-only sequential read-ahead; use `0`
for an A/B run with synchronous reads. It opens a second file handle, keeps two
64 KiB staging buffers, permits one reader request at a time, and only starts
after a run of ascending page accesses. A random page transition cancels queued/ready
pages. The worker never writes the 8 MiB cache; the emulation thread copies a
ready matching page using a nonblocking mutex attempt and otherwise performs
the existing synchronous read. Prefetch counters distinguish issued, used,
wasted, bytes read, and avoided synchronous reads. Prefetch seeks, reads, and
bytes are included in color-cache totals; asynchronous I/O wall time is
reported separately so the frame CPU estimate subtracts only I/O that blocked
the emulation thread.

The profiler remains independently controlled by `PS3_PGM_PERF_PROFILE=1`.

The audio figure covers post-emulation DSP and frontend submission. Sound-chip
emulation runs inside the game frame and is included in total frame time; this
initial profiler does not attribute that work separately.
