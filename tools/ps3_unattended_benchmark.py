#!/usr/bin/env python3
"""
PS3 FBNeo Unattended Benchmark Runner
- Automatically syncs ROMs and deploys CellSDK475 benchmark core.
- Configures in-core benchmark parameters and auto-input sequences.
- Launches game via webMAN MOD HTTP API.
- Monitors progress via remote HTTP/FTP watchdog.
- In case of freeze/crash, triggers webMAN /restart.ps3 to automatically recover.
- Harvests and parses frame-time, FPS, DRAM user memory, and allocator metrics into Markdown.
"""

import argparse
import datetime
import ftplib
import io
import os
import pathlib
import sys
import time
import urllib.request
import urllib.error

DEFAULT_PS3_IP = "192.168.2.230"
DEFAULT_ROM_DIR = "/mnt/f/FBNeo_Roms_no_clones"
REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILT_SELF = REPO_ROOT / "build-cellsdk" / "fbneo_libretro_psl1ght.self"
REPORTS_DIR = REPO_ROOT / "reports"

REMOTE_CORE_PATH = "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self"
REMOTE_CORE_BAK = "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self.orig_bak"
REMOTE_ROMS_DIR = "/dev_hdd0/ROMS"
REMOTE_TMP_DIR = "/dev_hdd0/tmp"
REMOTE_CFG_PATH = f"{REMOTE_TMP_DIR}/fbneo_benchmark.cfg"
REMOTE_REPORT_PATH = f"{REMOTE_TMP_DIR}/fbneo_benchmark_report.txt"
REMOTE_MEMLOG_PATH = f"{REMOTE_TMP_DIR}/fbneo-ps3-memory.log"


def webman_get(ip: str, endpoint: str, timeout: float = 5.0) -> str:
    url = f"http://{ip}{endpoint}"
    req = urllib.request.Request(url, headers={"User-Agent": "FBNeoBenchmark/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.read().decode("utf-8", errors="replace")
    except Exception as e:
        return f"ERROR: {e}"


def check_ps3_online(ip: str) -> bool:
    try:
        ret = webman_get(ip, "/popup.ps3?msg=Benchmark_Online_Check", timeout=3.0)
        return "Message sent" in ret or "wMAN MOD" in ret or "OK" in ret
    except Exception:
        return False


def wait_for_ps3_online(ip: str, max_wait: float = 60.0) -> bool:
    print(f"[*] Waiting for PS3 at {ip} to come online (max {max_wait}s)...")
    start = time.time()
    while time.time() - start < max_wait:
        if check_ps3_online(ip):
            print(f"[+] PS3 {ip} is online and responsive.")
            return True
        time.sleep(2.0)
    print(f"[-] PS3 {ip} did not respond within {max_wait}s.")
    return False


def get_ftp(ip: str) -> ftplib.FTP:
    ftp = ftplib.FTP(timeout=10)
    ftp.connect(ip, 21)
    ftp.login()
    return ftp


def ftp_file_exists(ftp: ftplib.FTP, remote_path: str) -> tuple[bool, int]:
    try:
        size = ftp.size(remote_path)
        return (True, size) if size is not None else (True, 0)
    except Exception:
        return (False, 0)


def ftp_delete_if_exists(ftp: ftplib.FTP, remote_path: str):
    try:
        ftp.delete(remote_path)
    except Exception:
        pass


def deploy_core(ip: str, local_self: pathlib.Path):
    if not local_self.exists():
        raise FileNotFoundError(f"Built core not found at {local_self}")

    local_size = local_self.stat().st_size
    print(f"[*] Checking remote core on PS3 ({REMOTE_CORE_PATH})...")

    with get_ftp(ip) as ftp:
        exists, remote_size = ftp_file_exists(ftp, REMOTE_CORE_PATH)
        if exists:
            bak_exists, _ = ftp_file_exists(ftp, REMOTE_CORE_BAK)
            if not bak_exists:
                print(f"[*] Creating backup of original core to {REMOTE_CORE_BAK}...")
                try:
                    ftp.rename(REMOTE_CORE_PATH, REMOTE_CORE_BAK)
                except Exception as e:
                    print(f"[!] Note on backup rename: {e}")

        print(f"[*] Uploading CellSDK475 benchmark core ({local_size:,} bytes) via FTP...")
        with open(local_self, "rb") as f:
            ftp.storbinary(f"STOR {REMOTE_CORE_PATH}", f)
        print("[+] Core successfully deployed to PS3.")


def sync_rom(ip: str, rom_name: str, rom_source_dir: pathlib.Path) -> str:
    if not rom_name.endswith(".zip"):
        rom_name += ".zip"

    local_rom = rom_source_dir / rom_name
    remote_rom_path = f"{REMOTE_ROMS_DIR}/{rom_name}"

    with get_ftp(ip) as ftp:
        exists, remote_size = ftp_file_exists(ftp, remote_rom_path)
        if exists and local_rom.exists() and remote_size == local_rom.stat().st_size:
            print(f"[+] ROM {rom_name} already present on PS3 ({remote_size:,} bytes). Skipping upload.")
            return remote_rom_path

        if not local_rom.exists():
            if exists:
                print(f"[!] ROM not found in local source dir, but present on PS3 ({remote_rom_path}). Using PS3 copy.")
                return remote_rom_path
            raise FileNotFoundError(f"ROM {rom_name} not found in {rom_source_dir} and not on PS3.")

        local_size = local_rom.stat().st_size
        print(f"[*] Uploading ROM {rom_name} ({local_size:,} bytes) to {remote_rom_path}...")
        with open(local_rom, "rb") as f:
            ftp.storbinary(f"STOR {remote_rom_path}", f)
        print(f"[+] Uploaded {rom_name} successfully.")

    return remote_rom_path


def write_benchmark_config(
    ip: str,
    frames: int,
    warmup: int,
    auto_coin: int,
    coin_frame: int,
    auto_start: int,
    start_frame: int,
    auto_action: int,
    action_start_frame: int,
    exit_after_frames: int = 1
):
    cfg_content = (
        f"enabled=1\n"
        f"max_frames={frames}\n"
        f"warmup_frames={warmup}\n"
        f"auto_coin={auto_coin}\n"
        f"coin_frame={coin_frame}\n"
        f"coin_duration=10\n"
        f"auto_start={auto_start}\n"
        f"start_frame={start_frame}\n"
        f"start_duration=10\n"
        f"auto_action={auto_action}\n"
        f"action_start_frame={action_start_frame}\n"
        f"action_interval=4\n"
        f"exit_after_frames={exit_after_frames}\n"
        f"log_window_frames=300\n"
    )

    print(f"[*] Writing benchmark configuration ({frames} frames, auto-input enabled)...")
    with get_ftp(ip) as ftp:
        ftp_delete_if_exists(ftp, REMOTE_REPORT_PATH)
        bio = io.BytesIO(cfg_content.encode("utf-8"))
        ftp.storbinary(f"STOR {REMOTE_CFG_PATH}", bio)
    print(f"[+] Config stored at {REMOTE_CFG_PATH}.")


def parse_report_file(content: str) -> dict:
    metrics = {}
    for line in content.splitlines():
        line = line.strip()
        if "=" in line and not line.startswith("["):
            k, v = line.split("=", 1)
            metrics[k.strip()] = v.strip()
    return metrics


def reboot_ps3(ip: str):
    print(f"[!] Triggering webMAN reboot on PS3 ({ip})...")
    webman_get(ip, "/popup.ps3?msg=Antigravity_Watchdog_Recovery", timeout=2.0)
    webman_get(ip, "/restart.ps3", timeout=3.0)
    time.sleep(5.0)
    wait_for_ps3_online(ip, max_wait=60.0)


def run_benchmark(args) -> int:
    ip = args.ip
    rom = args.rom
    rom_dir = pathlib.Path(args.rom_dir)
    frames = args.frames
    timeout = args.timeout

    print("==================================================")
    print(f" PS3 FBNeo Unattended Benchmark: {rom}")
    print(f" Target PS3 IP: {ip}")
    print(f" Budget Frames: {frames} (~{frames/60:.1f}s)")
    print(f" Watchdog Timeout: {timeout}s")
    print("==================================================")

    if not check_ps3_online(ip):
        print(f"[!] PS3 at {ip} is not currently responding to webMAN.")
        if not wait_for_ps3_online(ip, max_wait=20.0):
            print("[-] Cannot proceed without online PS3.")
            return 1

    if args.deploy_core:
        deploy_core(ip, BUILT_SELF)

    remote_rom_path = sync_rom(ip, rom, rom_dir)

    write_benchmark_config(
        ip=ip,
        frames=frames,
        warmup=args.warmup,
        auto_coin=args.auto_coin,
        coin_frame=args.coin_frame,
        auto_start=args.auto_start,
        start_frame=args.start_frame,
        auto_action=args.auto_action,
        action_start_frame=args.action_start_frame,
        exit_after_frames=1
    )

    print(f"[*] Launching game via webMAN: {remote_rom_path}...")
    webman_get(ip, f"/popup.ps3?msg=Benchmarking_{rom}_{frames}frames", timeout=3.0)
    time.sleep(1.0)
    launch_resp = webman_get(ip, f"/mount.ps3{remote_rom_path}", timeout=5.0)
    print(f"[*] Mount response received. Benchmark in progress...")

    start_time = time.time()
    report_content = None
    completed = False

    while time.time() - start_time < timeout:
        elapsed = time.time() - start_time
        time.sleep(3.0)

        # Check for report via FTP
        try:
            with get_ftp(ip) as ftp:
                exists, size = ftp_file_exists(ftp, REMOTE_REPORT_PATH)
                if exists and size > 200:
                    bio = io.BytesIO()
                    ftp.retrbinary(f"RETR {REMOTE_REPORT_PATH}", bio.write)
                    txt = bio.getvalue().decode("utf-8", errors="replace")
                    if "[PS3 BENCHMARK REPORT END]" in txt:
                        report_content = txt
                        completed = True
                        break
        except Exception:
            pass

        print(f"  > Waiting for benchmark completion... elapsed={elapsed:.1f}s", end="\r")

    print()
    if not completed:
        print(f"[!] WATCHDOG TRIGGERED: Benchmark timed out after {timeout}s without report!")
        print("[!] The core likely crashed or hung during execution.")
        reboot_ps3(ip)
        return 2

    print("[+] Benchmark completed cleanly on PS3!")

    # Download memory log as well
    mem_log_content = ""
    try:
        with get_ftp(ip) as ftp:
            bio = io.BytesIO()
            ftp.retrbinary(f"RETR {REMOTE_MEMLOG_PATH}", bio.write)
            mem_log_content = bio.getvalue().decode("utf-8", errors="replace")
    except Exception as e:
        print(f"[!] Note: could not download memory log: {e}")

    metrics = parse_report_file(report_content)

    REPORTS_DIR.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    report_path = REPORTS_DIR / f"benchmark_{metrics.get('game', rom)}_{timestamp}.md"

    md_content = f"""# PS3 FBNeo Benchmark Report: {metrics.get('driver', rom)}

- **Date**: {datetime.datetime.now().isoformat()}
- **Target PS3**: `{ip}`
- **ROM**: `{metrics.get('game', rom)}` ({remote_rom_path})
- **Hardware Architecture**: `{metrics.get('hardware', 'unknown')}`
- **SDK**: `Cell SDK 4.75 (Official PPU GCC 4.1.1 + PSGL ultra-opt)`

## Performance Summary

| Metric | Measured Value | Standard Target (60 FPS) | Status |
| :--- | :--- | :--- | :--- |
| **Wall Clock FPS** | **{metrics.get('wall_fps', 'N/A')} FPS** | 60.00 FPS | {'✅ Full Speed' if float(metrics.get('wall_fps', 0)) >= 59.0 else '⚠️ Slowdown'} |
| **Emulation Core FPS** | **{metrics.get('emulation_fps', 'N/A')} FPS** | >= 60.00 FPS | {'✅ Overhead Headroom' if float(metrics.get('emulation_fps', 0)) >= 60.0 else '⚠️ Core Bound'} |
| **Average Frame Time** | **{metrics.get('frame_time_avg_ms', 'N/A')} ms** | 16.667 ms | - |
| **Min Frame Time** | **{metrics.get('frame_time_min_ms', 'N/A')} ms** | - | - |
| **Max Frame Time (Stutter Peak)** | **{metrics.get('frame_time_max_ms', 'N/A')} ms** | < 25.0 ms | {'✅ Smooth' if float(metrics.get('frame_time_max_ms', 999)) < 30.0 else '⚠️ Stutter Peak'} |
| **Benchmarked Frames** | {metrics.get('benchmarked_frames', 'N/A')} frames | - | Fixed Budget |

## Memory Metrics (XDR DRAM 256 MiB Budget)

| Memory Arena | Consumption |
| :--- | :--- |
| **Initial Available User Memory** | {int(metrics.get('user_memory_initial', 0)) / (1024*1024):.2f} MiB |
| **Final Available User Memory** | {int(metrics.get('user_memory_final', 0)) / (1024*1024):.2f} MiB |
| **Net DRAM Consumed** | **{int(metrics.get('user_memory_consumed', 0)) / (1024*1024):.2f} MiB** |
| **Pool Total / Used / Peak** | {int(metrics.get('pool_total', 0)) / (1024*1024):.1f}M / {int(metrics.get('pool_used', 0)) / (1024*1024):.2f}M / {int(metrics.get('pool_peak', 0)) / (1024*1024):.2f}M |
| **Native Large Allocations Peak** | {int(metrics.get('native_peak', 0)) / (1024*1024):.2f} MiB |
| **System Heap Fallbacks** | {metrics.get('fallback_count', '0')} allocations ({int(metrics.get('system_peak', 0)) / (1024*1024):.2f} MiB peak) |
| **Allocation Failures (OOM)** | **{metrics.get('allocation_failures', '0')}** |

## Raw Benchmark Output
```text
{report_content.strip()}
```
"""
    with open(report_path, "w", encoding="utf-8") as f:
        f.write(md_content)

    print("\n" + md_content)
    print(f"[+] Full Markdown report saved to: {report_path}")
    return 0


def main():
    parser = argparse.ArgumentParser(description="PS3 FBNeo Unattended Benchmark Runner")
    parser.add_argument("--ip", default=DEFAULT_PS3_IP, help=f"PS3 IP address (default: {DEFAULT_PS3_IP})")
    parser.add_argument("--rom", default="kov.zip", help="ROM zip name (default: kov.zip)")
    parser.add_argument("--rom-dir", default=DEFAULT_ROM_DIR, help=f"Local ROM directory (default: {DEFAULT_ROM_DIR})")
    parser.add_argument("--frames", type=int, default=1800, help="Number of frames to benchmark (default: 1800 ~ 30s)")
    parser.add_argument("--warmup", type=int, default=60, help="Warmup frames to ignore in profiling (default: 60)")
    parser.add_argument("--auto-coin", type=int, default=1, help="Auto coin enable (default: 1)")
    parser.add_argument("--coin-frame", type=int, default=120, help="Frame to press coin (default: 120)")
    parser.add_argument("--auto-start", type=int, default=1, help="Auto start enable (default: 1)")
    parser.add_argument("--start-frame", type=int, default=180, help="Frame to press start (default: 180)")
    parser.add_argument("--auto-action", type=int, default=1, help="Auto action enable (default: 1)")
    parser.add_argument("--action-start-frame", type=int, default=240, help="Frame to begin action inputs (default: 240)")
    parser.add_argument("--timeout", type=int, default=90, help="Watchdog timeout seconds (default: 90)")
    parser.add_argument("--deploy-core", action="store_true", default=True, help="Deploy built CellSDK475 core (default: True)")
    parser.add_argument("--no-deploy-core", dest="deploy_core", action="store_false")

    args = parser.parse_args()
    sys.exit(run_benchmark(args))


if __name__ == "__main__":
    main()
