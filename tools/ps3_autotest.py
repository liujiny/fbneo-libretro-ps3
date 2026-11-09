#!/usr/bin/env python3
"""Unattended PS3 test runner for this FBNeo build.

Pipeline, with no interaction on the console:

  1. (optional) upload a freshly built core SELF,
  2. quit any running app back to the XMB via webMAN MOD,
  3. write /dev_hdd0/tmp/fbneo_launch_game.txt and fbneo_benchmark.cfg,
  4. launch the app with webMAN MOD (/mount.ps3/dev_hdd0/game/SSNE10001),
  5. the frontend auto-loads that content (platform_ps3.c hook), the in-core
     benchmark auto-coins, auto-starts, plays to max_frames and shuts the app
     down again,
  6. poll /dev_hdd0/tmp/fbneo_benchmark_report.txt over FTP and store it.

Usage:
  tools/ps3_autotest.py --game ddpdfk --frames 900
  tools/ps3_autotest.py --game mushisam --frames 2400 --wait 360 \
      --deploy build-cellsdk-auto/RetroArch_FBNeo_auto.self
"""

import argparse
import datetime
import ftplib
import io
import pathlib
import sys
import time
import urllib.error
import urllib.request

IP_DEFAULT = "192.168.2.230"
USRDIR = "/dev_hdd0/game/SSNE10001/USRDIR"
CORE = USRDIR + "/cores/fbneo_libretro_psl1ght.self"
APP_DIR = "/dev_hdd0/game/SSNE10001"
ROM_DIR = "/dev_hdd0/ROMS"
TMP = "/dev_hdd0/tmp"
CFG_PATH = TMP + "/fbneo_benchmark.cfg"
# The launcher reads this to decide which core to load; it gets rewritten by the
# frontend, so pin it before every run.
SALAMANDER_CFG = USRDIR + "/retroarch-salamander.cfg"

LAUNCH_PATH = TMP + "/fbneo_launch_game.txt"
REPORT_PATH = TMP + "/fbneo_benchmark_report.txt"
REPO = pathlib.Path(__file__).resolve().parents[1]
REPORT_DIR = REPO / "reports" / "bench"


def log(msg):
    print("[%s] %s" % (datetime.datetime.now().strftime("%H:%M:%S"), msg), flush=True)


def http_get(ip, path, timeout=20.0):
    url = "http://%s%s" % (ip, path)
    req = urllib.request.Request(url, headers={"User-Agent": "fbneo-autotest/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read(65536)
    except urllib.error.HTTPError as e:
        return e.code, b""
    except Exception as e:
        return None, str(e).encode()


def ftp(ip, timeout=30):
    f = ftplib.FTP()
    f.connect(ip, 21, timeout=timeout)
    f.login("anonymous", "anonymous")
    return f


def ftp_write(ip, remote, data):
    f = ftp(ip)
    try:
        f.storbinary("STOR " + remote, io.BytesIO(data))
    finally:
        f.quit()


def ftp_read(ip, remote):
    f = ftp(ip)
    try:
        buf = io.BytesIO()
        f.retrbinary("RETR " + remote, buf.write)
        return buf.getvalue()
    finally:
        f.quit()


def ftp_size(ip, remote):
    f = ftp(ip)
    try:
        try:
            return f.size(remote)
        except ftplib.error_perm:
            return None
    finally:
        f.quit()


def ftp_delete(ip, remote):
    f = ftp(ip)
    try:
        try:
            f.delete(remote)
        except ftplib.error_perm:
            pass
    finally:
        f.quit()


def app_running(ip):
    status, body = http_get(ip, "/cpursx.ps3", timeout=8.0)
    if status != 200:
        return None
    text = body.decode("utf-8", "ignore")
    # "(XMB)" is printed in the memory line when no app is loaded
    return "(XMB)" not in text


def wait_for_xmb(ip, timeout=40.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if app_running(ip) is False:
            return True
        time.sleep(2.0)
    return False


def deploy_core(ip, local_path):
    data = pathlib.Path(local_path).read_bytes()
    size = len(data)
    log("uploading core: %s (%d bytes)" % (local_path, size))
    remote_new = CORE + ".new"
    ftp_write(ip, remote_new, data)
    got = ftp_size(ip, remote_new)
    if got != size:
        raise RuntimeError("upload size mismatch: remote=%s local=%d" % (got, size))
    f = ftp(ip)
    try:
        try:
            f.delete(CORE)
        except ftplib.error_perm:
            pass
        f.rename(remote_new, CORE)
    finally:
        f.quit()
    log("core deployed")


def write_config(ip, frames, coin_frame, start_frame, action_frame,
                 coin_retry=120, start_retry=240, start_retry_count=8, warmup=None,
                 save_state_frame=0, load_state=0):
    """Retry mode: coin/start pulses are repeated on an interval, so the game
    picks them up as soon as its title screen accepts input. No per-game
    timing tuning is required."""
    cfg = "\n".join([
        "enabled=1",
        "max_frames=%d" % frames,
        "warmup_frames=%d" % (warmup if warmup is not None else max(60, frames // 8)),
        "auto_coin=1",
        "coin_frame=%d" % coin_frame,
        "coin_duration=30",
        "coin_retry_interval=%d" % coin_retry,
        "auto_start=1",
        "start_frame=%d" % start_frame,
        "start_duration=20",
        "start_retry_interval=%d" % start_retry,
        "start_retry_count=%d" % start_retry_count,
        "auto_action=1",
        "action_start_frame=%d" % action_frame,
        "action_interval=4",
        "exit_after_frames=1",
        "log_window_frames=120",
        "save_state_frame=%d" % save_state_frame,
        "load_state=%d" % load_state,
        "",
    ])
    ftp_write(ip, CFG_PATH, cfg.encode())
    log("benchmark config written (max_frames=%d)" % frames)


def pin_launcher_core(ip):
    """Point the launcher at the FBNeo core; the frontend rewrites this file."""
    text = 'libretro_path = "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self"\n'
    try:
        raw = ftp_read(ip, SALAMANDER_CFG)
        if isinstance(raw, bytes):
            raw = raw.decode("utf-8", "replace")
        if raw.strip() == text.strip():
            return False
    except Exception:
        pass
    ftp_write(ip, SALAMANDER_CFG, text.encode())
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", default=IP_DEFAULT)
    ap.add_argument("--game", required=True, help="rom name without .zip, e.g. ddpdfk")
    ap.add_argument("--frames", type=int, default=900)
    ap.add_argument("--wait", type=int, default=300, help="seconds to wait for the report")
    ap.add_argument("--deploy", help="local SELF to upload before the run")
    ap.add_argument("--keep-report", action="store_true", help="do not delete an old report")
    ap.add_argument("--coin-frame", type=int, default=None)
    ap.add_argument("--coin-retry", type=int, default=120, help="coin retry interval (0 for single pulse)")
    ap.add_argument("--start-frame", type=int, default=None)
    ap.add_argument("--start-retry", type=int, default=240, help="start retry interval (0 for single pulse)")
    ap.add_argument("--action-frame", type=int, default=None)
    ap.add_argument("--warmup", type=int, default=None, help="frames to skip before measuring")
    ap.add_argument("--save-state-frame", type=int, default=0, help="save a core state at this frame")
    ap.add_argument("--load-state", type=int, default=0, help="restore the saved state at startup")
    args = ap.parse_args()

    ip = args.ip
    rom = args.game if args.game.endswith(".zip") else args.game + ".zip"
    rom_path = "%s/%s" % (ROM_DIR, rom)

    coin = args.coin_frame if args.coin_frame is not None else 120
    start = args.start_frame if args.start_frame is not None else 240
    action = args.action_frame if args.action_frame is not None else 400
    warmup = args.warmup

    log("target: %s" % rom_path)

    if args.deploy:
        if app_running(ip):
            log("app is running, quitting to XMB first")
            http_get(ip, "/xmb.ps3$exit")
            if not wait_for_xmb(ip):
                raise RuntimeError("could not get back to the XMB")
        deploy_core(ip, args.deploy)

    if not args.keep_report:
        ftp_delete(ip, REPORT_PATH)

    write_config(ip, args.frames, coin, start, action,
                 coin_retry=args.coin_retry, start_retry=args.start_retry,
                 warmup=warmup,
                 save_state_frame=args.save_state_frame, load_state=args.load_state)
    ftp_write(ip, LAUNCH_PATH, (rom_path + "\n").encode())
    log("launch target written")

    if app_running(ip):
        log("app already running; quitting so it restarts with the new target")
        http_get(ip, "/xmb.ps3$exit")
        if not wait_for_xmb(ip):
            raise RuntimeError("could not get back to the XMB")

    # The frontend rewrites retroarch-salamander.cfg when it exits, and that happens
    # after the quit above, so pin the core again here or the launcher loads the
    # previous core (mame2003) instead of FBNeo.
    if pin_launcher_core(ip):
        log("launcher core re-pinned to fbneo")

    # webMAN's launch is flaky: it answers 200 even when the app does not start,
    # and the boot log not appearing means the core never ran. Retry until the
    # process is up.
    launched = False
    for attempt in range(1, 7):
        status, _ = http_get(ip, "/mount.ps3" + APP_DIR + "/")
        log("webMAN mount -> %s (attempt %d)" % (status, attempt))
        time.sleep(15.0)
        if not app_running(ip):
            continue
        launched = True
        break
    if not launched:
        log("could not start the app after 6 attempts")

    deadline = time.time() + args.wait
    size = None
    while time.time() < deadline:
        time.sleep(5.0)
        size = ftp_size(ip, REPORT_PATH)
        if size:
            log("report appeared (%d bytes)" % size)
            break
    if not size:
        log("NO REPORT after %ds - run failed or hung" % args.wait)
        return 2

    raw = ftp_read(ip, REPORT_PATH).decode("utf-8", "ignore")
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    out = REPORT_DIR / ("%s_%s.txt" % (args.game, stamp))
    out.write_text(raw, encoding="utf-8")

    metrics = {}
    for line in raw.splitlines():
        if "=" in line and not line.startswith("["):
            k, v = line.split("=", 1)
            metrics[k.strip()] = v.strip()

    log("saved: %s" % out)
    for key in ("game", "hardware", "benchmarked_frames", "wall_fps", "emulation_fps",
                "frame_time_avg_ms", "frame_time_max_ms", "allocation_failures",
                "system_live", "user_memory_final"):
        if key in metrics:
            print("  %-22s %s" % (key, metrics[key]))

    log("console state: %s" % ("XMB (app exited)" if app_running(ip) is False else "still running"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
