#!/usr/bin/env python3
"""Deploy latest FBNeo CV1000 builds and PKGs to PS3 console."""

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
        url = f"http://{ip}/browser.ps3$quit"
        req = urllib.request.Request(url, headers={"User-Agent": "ps3-deploy/1.0"})
        with urllib.request.urlopen(req, timeout=5) as r:
            pass
        log("Sent quit command to webMAN, waiting for XMB...")
        time.sleep(5)
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

    # Verify size
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
    log(f"=== Starting PS3 Deployment to {IP} ===")
    online, is_xmb = check_webman(IP)
    if not online:
        log(f"Error: PS3 at {IP} is not reachable!")
        sys.exit(1)

    if not is_xmb:
        quit_to_xmb(IP)

    ftp = get_ftp(IP)

    # 1. Deploy latest RSX GPU core and autoboot launcher to SSNE10001
    core_self = ROOT / "build-cellsdk-sh3rec" / "rel.self"
    if core_self.exists():
        ensure_dir(ftp, "/dev_hdd0/game/SSNE10001/USRDIR/cores")
        upload_file_atomic(ftp, core_self, "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self")

    launcher_bin = ROOT / "build-cellsdk" / "EBOOT_NPDRM.BIN"
    if launcher_bin.exists():
        upload_file_atomic(ftp, launcher_bin, "/dev_hdd0/game/SSNE10001/USRDIR/EBOOT.BIN")

    # 2. Upload standalone PKGs to /dev_hdd0/packages
    ensure_dir(ftp, "/dev_hdd0/packages")
    ddpdfk_pkg = ROOT / "packages" / "DoDonPachi_DaiFukkatsu_PS3.pkg"
    if ddpdfk_pkg.exists():
        upload_file_atomic(ftp, ddpdfk_pkg, "/dev_hdd0/packages/DoDonPachi_DaiFukkatsu_PS3.pkg")

    mmpork_pkg = ROOT / "packages" / "MuchiMuchiPork_PS3.pkg"
    if mmpork_pkg.exists():
        upload_file_atomic(ftp, mmpork_pkg, "/dev_hdd0/packages/MuchiMuchiPork_PS3.pkg")

    # 3. Deploy standalone title CAVE00001 (DoDonPachi Dai-Fukkatsu)
    log("=== Deploying CAVE00001 (DDPDFK Standalone) ===")
    ensure_dir(ftp, "/dev_hdd0/game/CAVE00001/USRDIR/cores")
    upload_file_atomic(ftp, ROOT / "packages/ddpdfk/PARAM.SFO", "/dev_hdd0/game/CAVE00001/PARAM.SFO")
    upload_file_atomic(ftp, ROOT / "packages/ddpdfk/ICON0.PNG", "/dev_hdd0/game/CAVE00001/ICON0.PNG")
    if launcher_bin.exists():
        upload_file_atomic(ftp, launcher_bin, "/dev_hdd0/game/CAVE00001/USRDIR/EBOOT.BIN")
    upload_file_atomic(ftp, ROOT / "build-cellsdk-sh3rec/rel.self", "/dev_hdd0/game/CAVE00001/USRDIR/cores/fbneo_libretro_psl1ght.self")

    # 4. Deploy standalone title CAVE00002 (Muchi Muchi Pork!)
    log("=== Deploying CAVE00002 (MMPork Standalone) ===")
    ensure_dir(ftp, "/dev_hdd0/game/CAVE00002/USRDIR/cores")
    upload_file_atomic(ftp, ROOT / "packages/mmpork/PARAM.SFO", "/dev_hdd0/game/CAVE00002/PARAM.SFO")
    upload_file_atomic(ftp, ROOT / "packages/mmpork/ICON0.PNG", "/dev_hdd0/game/CAVE00002/ICON0.PNG")
    if launcher_bin.exists():
        upload_file_atomic(ftp, launcher_bin, "/dev_hdd0/game/CAVE00002/USRDIR/EBOOT.BIN")
    upload_file_atomic(ftp, ROOT / "build-cellsdk-mmpork/mmpork.self", "/dev_hdd0/game/CAVE00002/USRDIR/cores/fbneo_libretro_psl1ght.self")
    upload_file_atomic(ftp, ROOT / "build-cellsdk-mmpork/mmpork.self", "/dev_hdd0/game/CAVE00002/USRDIR/cores/fbneo_libretro_psl1ght.self")

    ftp.quit()

    # Refresh webMAN XML so new titles appear in Games list
    try:
        log("Refreshing webMAN content list...")
        urllib.request.urlopen(f"http://{IP}/refresh.ps3?xmb", timeout=5)
    except:
        pass

    log("=== All deployments completed successfully! ===")

if __name__ == "__main__":
    main()
