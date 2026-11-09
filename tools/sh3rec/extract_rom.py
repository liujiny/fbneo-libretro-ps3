#!/usr/bin/env python3
import os
import sys
import zipfile
import struct

def extract_ddpdfk_rom(zip_path, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    out_bin = os.path.join(out_dir, "ddpdfk_program.bin")
    
    print(f"Opening ROM archive: {zip_path}")
    with zipfile.ZipFile(zip_path, 'r') as z:
        # ddpdfk_u4 is the 4MB SH-3 program ROM
        raw = z.read("ddpdfk_u4")
        print(f"Extracted ddpdfk_u4: {len(raw)} bytes")
        
        # Byte-swap for 16-bit Big-Endian SH-3 words
        # Raw ROM stores low byte first, SH-3 fetches big-endian words
        words = struct.unpack(f"<{len(raw)//2}H", raw)
        be_data = struct.pack(f">{len(words)}H", *words)
        
        with open(out_bin, "wb") as f:
            f.write(be_data)
        print(f"Wrote byte-swapped SH-3 binary to: {out_bin} ({len(be_data)} bytes)")
        
        # Also extract sound ROMs u23 and u24 if present
        for snd in ["u23", "u24"]:
            if snd in z.namelist():
                snd_data = z.read(snd)
                snd_path = os.path.join(out_dir, f"{snd}.bin")
                with open(snd_path, "wb") as f:
                    f.write(snd_data)
                print(f"Extracted {snd}: {len(snd_data)} bytes")

if __name__ == "__main__":
    zip_path = sys.argv[1] if len(sys.argv) > 1 else "/mnt/f/FBNeo_Roms/mame/ddpdfk.zip"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "roms/ddpdfk"
    extract_ddpdfk_rom(zip_path, out_dir)
