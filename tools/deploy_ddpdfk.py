#!/usr/bin/env python3
"""Deploy DoDonPachi Dai-Fukkatsu (ddpdfk) standalone & core to PS3."""

import os
import sys
import time
import ftplib
import pathlib
import urllib.request

IP = sys.argv[1] if len(sys.argv) > 1 else "192.168.2.231"
ROOT = pathlib.Path(__file__).resolve().parents[1]

def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)

def check_webman(ip):
    try:
        url = f"http://{ip}/cpursx.ps3"
        req = urllib.request.Request(url, headers={"User-Agent": "ps3-deploy/1.0"})
        with urllib.request.urlopen(req, timeout=5) as r:
            text = r.read().decode("utf-8", "ignore")
            is_xmb = "(XMB)" in text
            log(f"webMAN MOD online. Mode: {'XMB' if is_xmb else 'In-Game/App'}")
            return True, is_xmb
    except Exception as e:
        log(f"webMAN check failed: {e}")
        return False, False

def quit_to_xmb(ip):
    try:
        url = f"http://{ip}/xmb.ps3$exit"
        req = urllib.request.Request(url, headers={"User-Agent": "ps3-deploy/1.0"})
        with urllib.request.urlopen(req, timeout=5) as r:
            pass
        log("Sent quit command to webMAN, waiting for XMB...")
        for _ in range(20):
            time.sleep(2)
            online, is_xmb = check_webman(ip)
            if online and is_xmb:
                log("Confirmed returned to XMB.")
                return
    except Exception as e:
        log(f"Quit command warning: {e}")

def get_ftp(ip):
    ftp = ftplib.FTP()
    ftp.connect(ip, 21, timeout=30)
    ftp.login("anonymous", "anonymous")
    return ftp

def upload_file_atomic(ftp, local_path, remote_path):
    local_path = pathlib.Path(local_path)
    total_size = local_path.stat().st_size
    log(f"Uploading {local_path.name} ({total_size / (1024*1024):.1f} MB) -> {remote_path} ...")

    remote_tmp = remote_path + ".upload"
    try:
        ftp.delete(remote_tmp)
    except:
        pass

    uploaded = 0
    t0 = time.time()
    last_t = t0

    def callback(chunk):
        nonlocal uploaded, last_t
        uploaded += len(chunk)
        now = time.time()
        if now - last_t >= 2.0 or uploaded == total_size:
            speed = (uploaded / (1024*1024)) / max(0.001, now - t0)
            pct = (uploaded / total_size) * 100
            print(f"  {uploaded / (1024*1024):.1f} / {total_size / (1024*1024):.1f} MB ({pct:.1f}%) @ {speed:.2f} MB/s", end="\r", flush=True)
            last_t = now

    with open(local_path, "rb") as f:
        ftp.storbinary(f"STOR {remote_tmp}", f, blocksize=128*1024, callback=callback)
    print()

    # Re-connect if control connection dropped
    try:
        rem_size = ftp.size(remote_tmp)
    except Exception:
        ftp = get_ftp(IP)
        rem_size = ftp.size(remote_tmp)

    if rem_size != total_size:
        raise RuntimeError(f"Size mismatch on {remote_tmp}: local={total_size}, remote={rem_size}")

    try:
        ftp.delete(remote_path)
    except:
        pass

    ftp.rename(remote_tmp, remote_path)
    log(f"Successfully deployed {remote_path} ({rem_size} bytes)")

def ensure_dir(ftp, remote_dir):
    parts = remote_dir.strip("/").split("/")
    curr = ""
    for p in parts:
        curr += "/" + p
        try:
            ftp.mkd(curr)
        except:
            pass

def main():
    log(f"=== Starting DoDonPachi Dai-Fukkatsu (ddpdfk) Deploy to {IP} ===")
    online, is_xmb = check_webman(IP)
    if not online:
        log(f"Error: PS3 at {IP} is not reachable!")
        sys.exit(1)

    if not is_xmb:
        quit_to_xmb(IP)

    ftp = get_ftp(IP)

    # 1. Deploy core to SSNE10001
    core_self = ROOT / "build-cellsdk-drc-gpu" / "rel.self"
    if not core_self.exists():
        log(f"Error: {core_self} does not exist!")
        sys.exit(1)

    ensure_dir(ftp, "/dev_hdd0/game/SSNE10001/USRDIR/cores")
    upload_file_atomic(ftp, core_self, "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self")

    launcher_bin = ROOT / "ps3_dualcore_pkg_20260928" / "SSNE10001" / "USRDIR" / "EBOOT.BIN"
    if launcher_bin.exists():
        upload_file_atomic(ftp, launcher_bin, "/dev_hdd0/game/SSNE10001/USRDIR/EBOOT.BIN")

    # 2. Deploy standalone CAVE00001 (DoDonPachi Dai-Fukkatsu)
    log("=== Deploying CAVE00001 (DDPDFK Standalone) ===")
    ensure_dir(ftp, "/dev_hdd0/game/CAVE00001/USRDIR/cores")
    upload_file_atomic(ftp, ROOT / "packages/ddpdfk/PARAM.SFO", "/dev_hdd0/game/CAVE00001/PARAM.SFO")
    upload_file_atomic(ftp, ROOT / "packages/ddpdfk/ICON0.PNG", "/dev_hdd0/game/CAVE00001/ICON0.PNG")
    if launcher_bin.exists():
        upload_file_atomic(ftp, launcher_bin, "/dev_hdd0/game/CAVE00001/USRDIR/EBOOT.BIN")
    upload_file_atomic(ftp, core_self, "/dev_hdd0/game/CAVE00001/USRDIR/cores/fbneo_libretro_psl1ght.self")

    # 3. Upload standalone PKG to /dev_hdd0/packages
    ensure_dir(ftp, "/dev_hdd0/packages")
    ddpdfk_pkg = ROOT / "packages" / "DoDonPachi_DaiFukkatsu_PS3.pkg"
    if ddpdfk_pkg.exists():
        upload_file_atomic(ftp, ddpdfk_pkg, "/dev_hdd0/packages/DoDonPachi_DaiFukkatsu_PS3.pkg")

    # 4. Set launch target and pin retroarch-salamander.cfg
    ensure_dir(ftp, "/dev_hdd0/tmp")
    import io
    ftp.storbinary("STOR /dev_hdd0/tmp/fbneo_launch_game.txt", io.BytesIO(b"/dev_hdd0/ROMS/ddpdfk.zip\n"))
    log("Set /dev_hdd0/tmp/fbneo_launch_game.txt -> /dev_hdd0/ROMS/ddpdfk.zip")

    salamander_cfg_ssne = 'libretro_path = "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self"\n'
    ftp.storbinary("STOR /dev_hdd0/game/SSNE10001/USRDIR/retroarch-salamander.cfg", io.BytesIO(salamander_cfg_ssne.encode()))
    log("Pinned /dev_hdd0/game/SSNE10001/USRDIR/retroarch-salamander.cfg")

    salamander_cfg_cave = 'libretro_path = "/dev_hdd0/game/CAVE00001/USRDIR/cores/fbneo_libretro_psl1ght.self"\n'
    ftp.storbinary("STOR /dev_hdd0/game/CAVE00001/USRDIR/retroarch-salamander.cfg", io.BytesIO(salamander_cfg_cave.encode()))
    log("Pinned /dev_hdd0/game/CAVE00001/USRDIR/retroarch-salamander.cfg")

    # Remove any old stale benchmark report
    try:
        ftp.delete("/dev_hdd0/tmp/fbneo_benchmark_report.txt")
    except:
        pass

    ftp.quit()
    log("=== Deploy Complete! ===")

if __name__ == "__main__":
    main()
