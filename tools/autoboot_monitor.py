#!/usr/bin/env python3
import time
import socket
import urllib.request
import urllib.error
import ftplib
import os
from PIL import Image

PS3_IP = "192.168.2.230"

def ping_ps3(timeout=2):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((PS3_IP, 80))
        s.close()
        return True
    except Exception:
        return False

def grab_screenshot(out_png):
    try:
        url = f"http://{PS3_IP}/browser.ps3$screenshot_xmb?show?fast"
        req = urllib.request.Request(url, headers={'User-Agent': 'curl/7.68.0'})
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = resp.read()
        tmp_bmp = out_png.replace(".png", ".bmp")
        with open(tmp_bmp, "wb") as f:
            f.write(data)
        im = Image.open(tmp_bmp)
        im.save(out_png)
        os.remove(tmp_bmp)
        return True
    except Exception as e:
        return False

def check_report():
    try:
        url = f"http://{PS3_IP}/dev_hdd0/tmp/fbneo_benchmark_report.txt"
        with urllib.request.urlopen(url, timeout=3) as resp:
            text = resp.read().decode("utf-8", errors="ignore")
            if "[PS3 BENCHMARK REPORT END]" in text:
                return text
    except Exception:
        pass
    return None

def main():
    print(f"[*] Triggering PS3 restart via http://{PS3_IP}/restart.ps3 ...")
    try:
        urllib.request.urlopen(f"http://{PS3_IP}/restart.ps3", timeout=5)
    except Exception as e:
        print(f"[*] Restart signal sent (response: {e})")

    print("[*] Waiting for PS3 to go down...")
    time.sleep(5)
    down_time = time.time()
    while ping_ps3(timeout=1):
        if time.time() - down_time > 20:
            print("[!] PS3 still responding after 20s...")
            break
        time.sleep(1)

    print("[*] PS3 is rebooting. Waiting for it to come back online...")
    boot_start = time.time()
    while not ping_ps3(timeout=2):
        if time.time() - boot_start > 90:
            print("[!] Timeout waiting for PS3 to come back online (90s)!")
            return False
        time.sleep(2)

    print(f"[+] PS3 is back online! Took {time.time()-boot_start:.1f}s.")
    print("[*] Monitoring for Autoboot and Benchmark execution...")

    monitor_start = time.time()
    last_snap = 0
    snap_count = 0

    while time.time() - monitor_start < 240:
        elapsed = time.time() - monitor_start

        # Check for benchmark report
        rep = check_report()
        if rep:
            print(f"\n[🎉 SUCCESS] Benchmark Report detected at T+{elapsed:.1f}s!")
            print("==================================================")
            print(rep)
            print("==================================================")
            with open("/tmp/completed_benchmark_report.txt", "w") as f:
                f.write(rep)
            return True

        # Check cpursx
        try:
            with urllib.request.urlopen(f"http://{PS3_IP}/cpursx.ps3", timeout=3) as resp:
                txt = resp.read().decode("utf-8", errors="ignore")
                mem_line = [l for l in txt.splitlines() if "MEM:" in l]
                if mem_line:
                    import re
                    clean_mem = re.sub('<[^<]+?>', '', mem_line[0]).strip()
                    print(f"[T+{elapsed:.1f}s] {clean_mem}")
        except Exception:
            pass

        # Capture a screenshot every 15s
        if time.time() - last_snap >= 15:
            last_snap = time.time()
            snap_count += 1
            snap_file = f"/tmp/snap_{snap_count}.png"
            if grab_screenshot(snap_file):
                print(f"[+] Captured {snap_file}")

        time.sleep(4)

    print("[!] Timeout waiting for benchmark to complete (240s).")
    return False

if __name__ == "__main__":
    main()
