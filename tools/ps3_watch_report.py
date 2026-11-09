#!/usr/bin/env python3
import ftplib
import io
import time
import sys
import datetime
import pathlib

IP = "192.168.2.230"
REMOTE_REPORT = "/dev_hdd0/tmp/fbneo_benchmark_report.txt"
REMOTE_MEMLOG = "/dev_hdd0/tmp/fbneo-ps3-memory.log"
REPORTS_DIR = pathlib.Path(__file__).resolve().parents[1] / "reports"

def parse_report(content):
    metrics = {}
    for line in content.splitlines():
        line = line.strip()
        if "=" in line and not line.startswith("["):
            k, v = line.split("=", 1)
            metrics[k.strip()] = v.strip()
    return metrics

def format_bytes(n):
    try:
        val = float(n)
        if val >= 1024 * 1024:
            return f"{val / (1024 * 1024):.2f} MiB ({int(val):,} B)"
        elif val >= 1024:
            return f"{val / 1024:.2f} KiB ({int(val):,} B)"
        return f"{int(val)} B"
    except Exception:
        return str(n)

def main():
    timeout = 400
    start = time.time()
    print(f"[*] Watching for PS3 FBNeo benchmark report on {IP} (timeout={timeout}s)...")
    
    while time.time() - start < timeout:
        elapsed = time.time() - start
        try:
            ftp = ftplib.FTP(IP, timeout=3)
            ftp.login()
            ftp.cwd("/dev_hdd0/tmp")
            files = ftp.nlst()
            if "fbneo_benchmark_report.txt" in files:
                bio = io.BytesIO()
                ftp.retrbinary("RETR fbneo_benchmark_report.txt", bio.write)
                txt = bio.getvalue().decode("utf-8", errors="replace")
                if "[PS3 BENCHMARK REPORT END]" in txt:
                    print("\n[+] Benchmark report found and downloaded successfully!")
                    
                    # Also fetch memory log
                    mem_txt = ""
                    if "fbneo-ps3-memory.log" in files:
                        mbio = io.BytesIO()
                        ftp.retrbinary("RETR fbneo-ps3-memory.log", mbio.write)
                        mem_txt = mbio.getvalue().decode("utf-8", errors="replace")
                    
                    ftp.quit()
                    
                    metrics = parse_report(txt)
                    REPORTS_DIR.mkdir(parents=True, exist_ok=True)
                    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
                    game = metrics.get("game", "unknown")
                    rpt_file = REPORTS_DIR / f"benchmark_CellSDK475_{game}_{ts}.md"
                    
                    md = f"""# PS3 FBNeo Benchmark Report: {metrics.get('driver', game)}

- **Date**: {datetime.datetime.now().isoformat()}
- **Target PS3**: `{IP}`
- **ROM**: `{game}` ({metrics.get('rom_path', 'unknown')})
- **Hardware Profile**: `{metrics.get('hardware', 'unknown')}`
- **SDK**: `Cell SDK 4.75 (Official PPU GCC 4.1.1 + PSGL)`

## 1. Frame Rate & Timing Performance

| Metric | Measured Value | Standard Target (60 FPS) | Status |
| :--- | :--- | :--- | :--- |
| **Wall Clock FPS** | **{metrics.get('wall_fps', 'N/A')} FPS** | 60.00 FPS | {'✅ Full Speed' if float(metrics.get('wall_fps', 0)) >= 59.0 else '⚠️ Slowdown'} |
| **Emulation Core FPS** | **{metrics.get('emulation_fps', 'N/A')} FPS** | >= 60.00 FPS | {'✅ Overhead Headroom' if float(metrics.get('emulation_fps', 0)) >= 60.0 else '⚠️ Core Bound'} |
| **Average Frame Time** | **{metrics.get('frame_time_avg_ms', 'N/A')} ms** | 16.667 ms | - |
| **Min Frame Time** | **{metrics.get('frame_time_min_ms', 'N/A')} ms** | - | - |
| **Max Frame Time (Stutter Peak)** | **{metrics.get('frame_time_max_ms', 'N/A')} ms** | < 25.0 ms | {'✅ Smooth' if float(metrics.get('frame_time_max_ms', 999)) < 25.0 else '⚠️ Stutter Peak'} |
| **Benchmarked Frames** | {metrics.get('benchmarked_frames', 'N/A')} frames | 600 frames | Completed |

## 2. Memory Consumption (Cell OS 256 MiB XDR Budget)

| Memory Metric | Value | Details |
| :--- | :--- | :--- |
| **Initial Available User Memory** | {format_bytes(metrics.get('user_memory_initial', 0))} | Total free before core boot |
| **Final Available User Memory** | {format_bytes(metrics.get('user_memory_final', 0))} | Total free after game emulation |
| **Total Memory Consumed** | **{format_bytes(metrics.get('user_memory_consumed', 0))}** | Net memory footprint |
| **Static Pool Peak** | {format_bytes(metrics.get('pool_peak', 0))} / {format_bytes(metrics.get('pool_total', 0))} | Fixed 8 MiB Pre-allocated Pool |
| **Native Heap Live Peak** | {format_bytes(metrics.get('native_peak', 0))} | sys_memory / memalign allocations |
| **Allocation Failures** | **{metrics.get('allocation_failures', 0)}** | {'✅ No OOM' if int(metrics.get('allocation_failures', 0)) == 0 else '❌ OOM Detected'} |

## 3. Raw Benchmark Report Output

```text
{txt}
```
"""
                    rpt_file.write_text(md, encoding="utf-8")
                    print(f"[+] Saved markdown report to: {rpt_file}")
                    print("\n" + md)
                    return 0
            ftp.quit()
        except Exception as e:
            pass
        
        time.sleep(2)
        print(f"  > Waiting for benchmark to complete... elapsed={elapsed:.0f}s", end="\r", flush=True)

    print("\n[-] Timeout waiting for benchmark report.")
    return 1

if __name__ == "__main__":
    sys.exit(main())
