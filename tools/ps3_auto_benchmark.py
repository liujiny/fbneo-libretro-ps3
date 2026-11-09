#!/usr/bin/env python3
"""
PS3 FBNeo Fully-Automated Unattended Benchmark Runner
- Deploys Official Cell SDK 4.75 Benchmark Core
- Prepares target ROM and in-core benchmark sequence config
- Injects target ROM at top of RetroArch playlist
- Remotely boots RetroArch PSL1GHT via webMAN MOD HTTP API
- Remotely triggers game launch via webMAN Virtual Pad (/pad.ps3?accept)
- Watches progress, captures report and memory diagnostics
- Guarantees 0-touch automated testing from PC
"""

import argparse
import datetime
import ftplib
import io
import json
import os
import pathlib
import socket
import sys
import time
import urllib.request
import urllib.error

DEFAULT_PS3_IP = "192.168.2.230"
REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILT_SELF = REPO_ROOT / "build-cellsdk" / "fbneo_libretro_psl1ght.self"
REPORTS_DIR = REPO_ROOT / "reports"

REMOTE_CORE_PATH = "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self"
REMOTE_PLAYLIST = "/dev_hdd0/game/SSNE10001/USRDIR/content_history.lpl"
REMOTE_CFG_PATH = "/dev_hdd0/tmp/fbneo_benchmark.cfg"
REMOTE_REPORT_PATH = "/dev_hdd0/tmp/fbneo_benchmark_report.txt"
REMOTE_MEMLOG_PATH = "/dev_hdd0/tmp/fbneo-ps3-memory.log"

def webman_cmd(ip: str, path: str, timeout: float = 4.0) -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect((ip, 80))
        req = f"GET {path} HTTP/1.0\r\nHost: {ip}\r\nUser-Agent: Mozilla/5.0\r\nConnection: close\r\n\r\n"
        s.sendall(req.encode("utf-8"))
        chunks = []
        while True:
            try:
                c = s.recv(4096)
                if not c:
                    break
                chunks.append(c)
            except Exception:
                break
        s.close()
        return b"".join(chunks).decode("utf-8", errors="replace")
    except Exception as e:
        return f"ERROR: {e}"

def is_ps3_in_xmb(ip: str) -> bool:
    try:
        txt = webman_cmd(ip, "/cpursx.ps3", timeout=3.0)
        return "(XMB)" in txt
    except Exception:
        return False

def ensure_xmb(ip: str):
    if is_ps3_in_xmb(ip):
        return
    print("[*] Requesting return to XMB...")
    webman_cmd(ip, "/xmb.ps3$exit", timeout=3.0)
    for _ in range(15):
        time.sleep(1.0)
        if is_ps3_in_xmb(ip):
            print("[+] PS3 is in XMB.")
            return
    print("[!] Warning: PS3 not confirmed in XMB after 15s.")

def setup_history(ftp, rom_name: str, remote_rom_path: str):
    playlists = [
        "/dev_hdd0/game/SSNE10001/USRDIR/content_history.lpl",
        "/dev_hdd0/game/ARCD00001/USRDIR/content_history.lpl",
    ]
    for pl_path in playlists:
        bio = io.BytesIO()
        try:
            ftp.retrbinary(f"RETR {pl_path}", bio.write)
            data = json.loads(bio.getvalue().decode("utf-8"))
        except Exception:
            data = {"version": "1.5", "items": []}

        items = [it for it in data.get("items", []) if rom_name not in it.get("path", "")]
        target_entry = {
            "path": remote_rom_path,
            "label": f"{rom_name} (FBNeo Benchmark)",
            "core_path": REMOTE_CORE_PATH,
            "core_name": "Arcade (FinalBurn Neo)",
            "crc32": "",
            "db_name": ""
        }
        data["items"] = [target_entry] + items
        out_bytes = json.dumps(data, indent=2).encode("utf-8")
        try:
            ftp.storbinary(f"STOR {pl_path}", io.BytesIO(out_bytes))
            print(f"[+] Configured playlist top item in {pl_path}: {target_entry['label']}")
        except Exception as e:
            print(f"[!] Warning writing playlist {pl_path}: {e}")

def write_config(ftp, frames=2400, warmup=300, coin_frame=900, start_frame=1020, action_frame=1140):
    cfg_text = f"""enabled=1
max_frames={frames}
warmup_frames={warmup}
auto_coin=1
coin_frame={coin_frame}
coin_duration=30
auto_start=1
start_frame={start_frame}
start_duration=20
auto_action=1
action_start_frame={action_frame}
action_interval=4
exit_after_frames=1
log_window_frames=300
"""
    ftp.storbinary(f"STOR {REMOTE_CFG_PATH}", io.BytesIO(cfg_text.encode("utf-8")))
    print(f"[+] Configured benchmark params: frames={frames} (~{frames/60:.1f}s), coin={coin_frame}, start={start_frame}, action={action_frame}")

def clean_reports(ftp):
    try:
        ftp.cwd("/dev_hdd0/tmp")
        files = ftp.nlst()
        for f in ["fbneo_benchmark_report.txt", "boot_check_diag.log", "pad_diag.log", "fbneo-ps3-memory.log"]:
            if f in files:
                ftp.delete(f)
                print(f"[+] Cleaned previous {f}.")
    except Exception as e:
        print(f"[!] clean_reports note: {e}")

def parse_report(content: str) -> dict:
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

def run_unattended(rom: str, ip: str = DEFAULT_PS3_IP, frames: int = 2400, deploy: bool = False):
    print("==================================================")
    print(f" PS3 FBNeo Fully-Automated Benchmark Runner")
    print(f" Target ROM: {rom}")
    print(f" Target PS3 IP: {ip}")
    print(f" Target Frames: {frames} (~{frames/60:.1f}s)")
    print("==================================================")

    webman_cmd(ip, "/mount.ps3/unmount", timeout=3.0)
    ensure_xmb(ip)

    ftp = ftplib.FTP(ip, timeout=10)
    ftp.login()

    if deploy:
        print(f"[*] Deploying latest core {BUILT_SELF} ({BUILT_SELF.stat().st_size} bytes)...")
        targets = [
            "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self",
            "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.SELF",
            "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_ps3.self",
            "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_ps3.SELF",
            "/dev_hdd0/game/ARCD00001/USRDIR/cores/fbneo_libretro_ps3.self",
            "/dev_hdd0/game/ARCD00001/USRDIR/cores/fbneo_libretro_ps3.SELF",
            "/dev_hdd0/game/ARCD00001/USRDIR/cores/fbneo_libretro_psl1ght.self",
            "/dev_hdd0/game/ARCD00001/USRDIR/cores/fbneo_libretro_psl1ght.SELF",
        ]
        with open(BUILT_SELF, "rb") as f:
            core_bytes = f.read()
        for tgt in targets:
            try:
                ftp.storbinary(f"STOR {tgt}", io.BytesIO(core_bytes), blocksize=65536)
                print(f"[+] Deployed to {tgt}")
            except Exception as e:
                print(f"[!] Warning deploying to {tgt}: {e}")
        print("[+] Core multi-target deployment complete.")

    remote_rom_path = f"/dev_hdd0/ROMS/{rom}"
    # Write direct launch game target file
    ftp.storbinary("STOR /dev_hdd0/tmp/fbneo_launch_game.txt", io.BytesIO(remote_rom_path.encode("utf-8")))
    print(f"[+] Configured direct launch game target: {remote_rom_path}")

    setup_history(ftp, rom, remote_rom_path)
    write_config(ftp, frames=frames)
    clean_reports(ftp)
    ftp.quit()

    # Step 1: Map /app_home to SSNE10001 (RetroArch PSL1GHT)
    print("[*] Mapping /app_home to RetroArch PSL1GHT (SSNE10001)...")
    webman_cmd(ip, "/app_home.ps3?/dev_hdd0/game/SSNE10001", timeout=5.0)
    time.sleep(1.5)

    # Step 2: Trigger launch via Virtual Pad R2 + START (Play app_home combo)
    print("[*] Triggering PLAY APP_HOME via webMAN Virtual Pad (R2 + START)...")
    webman_cmd(ip, "/pad.ps3?r2_start", timeout=3.0)
    time.sleep(1.0)
    webman_cmd(ip, "/combo.ps3?r2_start", timeout=3.0)
    time.sleep(3.0)

    # Check if PS3 needs fallback launch via /play.ps3
    if is_ps3_in_xmb(ip):
        print("[*] PS3 still in XMB, sending fallback /play.ps3...")
        webman_cmd(ip, "/play.ps3", timeout=5.0)
        time.sleep(3.0)

    # Step 3: Redundant virtual pad confirmation for Asian PS3 (Enter/Circle)
    print("[*] Waiting for RetroArch PSL1GHT to initialize and boot game...")
    time.sleep(4.0)
    webman_cmd(ip, "/pad.ps3?accept", timeout=2.0)
    time.sleep(1.5)
    webman_cmd(ip, "/pad.ps3?accept", timeout=2.0)

    # Step 4: Watch loop (CV1000 runs ~30-40 fps, so give generous headroom)
    timeout = (frames / 20.0) + 60.0
    start = time.time()
    print(f"[*] Watching for benchmark completion (timeout={timeout:.0f}s)...")
    report_content = None

    while time.time() - start < timeout:
        elapsed = time.time() - start
        try:
            wftp = ftplib.FTP(ip, timeout=3)
            wftp.login()
            wftp.cwd("/dev_hdd0/tmp")
            files = wftp.nlst()
            if "fbneo_benchmark_report.txt" in files:
                bio = io.BytesIO()
                wftp.retrbinary("RETR fbneo_benchmark_report.txt", bio.write)
                txt = bio.getvalue().decode("utf-8", errors="replace")
                if "[PS3 BENCHMARK REPORT END]" in txt:
                    report_content = txt
                    print(f"\n[+] Benchmark report acquired after {elapsed:.1f}s!")
                    wftp.quit()
                    break
            wftp.quit()
        except Exception:
            pass

        time.sleep(2.0)
        # Check if PS3 prematurely dropped back to XMB after at least 30s
        if elapsed > 30.0 and is_ps3_in_xmb(ip):
            print(f"\n[!] EARLY EXIT DETECTED: PS3 returned to XMB after {elapsed:.1f}s without generating a report!")
            break

        print(f"  > Benchmarking in progress... elapsed={elapsed:.0f}s / budget={frames/60:.0f}s", end="\r", flush=True)

    if not report_content:
        print(f"\n[!] Timeout waiting for benchmark report!")
        return 1

    metrics = parse_report(report_content)
    REPORTS_DIR.mkdir(parents=True, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    game = metrics.get("game", rom)
    rpt_file = REPORTS_DIR / f"benchmark_CellSDK475_{game}_{ts}.md"

    md = f"""# PS3 FBNeo Benchmark Report: {metrics.get('driver', game)}

- **Date**: {datetime.datetime.now().isoformat()}
- **Target PS3**: `{ip}`
- **ROM**: `{game}` ({remote_rom_path})
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
| **Benchmarked Frames** | {metrics.get('benchmarked_frames', 'N/A')} frames | {metrics.get('total_frames', frames)} frames | Completed |

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
{report_content}
```
"""
    rpt_file.write_text(md, encoding="utf-8")
    print(f"\n[+] Saved markdown report to: {rpt_file}")
    print("\n" + md)

    # Confirm clean exit to XMB
    print("[*] Verifying return to XMB...")
    time.sleep(3.0)
    if is_ps3_in_xmb(ip):
        print("[+] PS3 cleanly returned to XMB without manual interaction!")
    else:
        print("[*] Bringing PS3 back to XMB...")
        webman_cmd(ip, "/xmb.ps3$exit", timeout=3.0)

    return 0

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", nargs="?", default="ddpdfk.zip", help="Target ROM filename")
    parser.add_argument("--ip", default=DEFAULT_PS3_IP, help="Target PS3 IP")
    parser.add_argument("--frames", type=int, default=2400, help="Benchmark budget frames")
    parser.add_argument("--deploy", action="store_true", help="Deploy newly built core before run")
    args = parser.parse_args()

    sys.exit(run_unattended(args.rom, ip=args.ip, frames=args.frames, deploy=args.deploy))
