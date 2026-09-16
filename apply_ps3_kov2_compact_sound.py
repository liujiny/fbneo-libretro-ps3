#!/usr/bin/env python3
"""
Apply a PS3-only KOV2 compact ICS2115 sound-ROM layout to fbneo-libretro-ps3.

Goal:
  Keep KOV2's logical ICS2115 address map unchanged:
      0x000000..0x1fffff : PGM BIOS samples
      0x200000..0x7fffff : unmapped hole
      0x800000..          : KOV2 samples
  while removing the 6 MiB hole from the physical XDR allocation.

The patch is intentionally conservative:
  * PS3-only (__PS3__)
  * exact driver name "kov2" only
  * disabled when IPS patching is active
  * non-PS3 paths remain unchanged
  * also improves the PGM lazy draw-buffer OOM diagnostic

Run from repository root, for example:
    cd /work
    python3 apply_ps3_kov2_compact_sound.py

Safe to run repeatedly: already-patched files are detected.
"""

from pathlib import Path
import sys

MARKER = "PS3_KOV2_COMPACT_SOUND"


def die(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def load(path: Path):
    if not path.is_file():
        die(f"missing file: {path}")
    data = path.read_bytes()
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        die(f"not UTF-8 text: {path}")
    nl = "\r\n" if b"\r\n" in data else "\n"
    return text.replace("\r\n", "\n"), nl


def save(path: Path, text: str, nl: str):
    if nl == "\r\n":
        text = text.replace("\n", "\r\n")
    path.write_bytes(text.encode("utf-8"))


def replace_once(text, old, new, desc):
    count = text.count(old)
    if count != 1:
        die(f"{desc}: expected exactly 1 anchor, found {count}")
    return text.replace(old, new, 1)


def patch_pgm_run(path: Path):
    text, nl = load(path)
    if MARKER in text:
        print(f"already patched: {path}")
        return False

    text = replace_once(
        text,
        "UINT8 *ICSSNDROM;\n",
        "UINT8 *ICSSNDROM;\n"
        "\n"
        "#ifdef __PS3__\n"
        f"// {MARKER}: keep KOV2 logical ICS2115 addresses while removing the 6 MiB XDR hole.\n"
        "static INT32 pgm_ps3_kov2_compact_sound()\n"
        "{\n"
        "\treturn (!bDoIpsPatch && strcmp(BurnDrvGetTextA(DRV_NAME), \"kov2\") == 0);\n"
        "}\n"
        "static INT32 nPGMSNDROMAllocLen = 0;\n"
        "#endif\n",
        "pgm_run: ICSSNDROM declaration",
    )

    text = replace_once(
        text,
        "\tUINT8 *PGMSNDROMLoad = ICSSNDROM + (kov2 ? 0x800000 : 0x400000);\n",
        "\tUINT8 *PGMSNDROMLoad = ICSSNDROM + (kov2 ? 0x800000 : 0x400000);\n"
        "#ifdef __PS3__\n"
        "\t// Physical KOV2 layout is compact on PS3: BIOS 2 MiB followed immediately by game samples.\n"
        "\t// The ICS2115 reader remaps logical >= 0x800000 back by 0x600000.\n"
        "\tif (bLoad && pgm_ps3_kov2_compact_sound()) {\n"
        "\t\tPGMSNDROMLoad = ICSSNDROM + 0x200000;\n"
        "\t}\n"
        "#endif\n",
        "pgm_run: PGMSNDROMLoad",
    )

    text = replace_once(
        text,
        "\tICSSNDROM\t\t= (UINT8*)BurnMalloc(nPGMSNDROMLen);\n",
        "#ifdef __PS3__\n"
        "\tnPGMSNDROMAllocLen = nPGMSNDROMLen;\n"
        "\tif (pgm_ps3_kov2_compact_sound() && nPGMSNDROMLen >= 0x800000) {\n"
        "\t\tnPGMSNDROMAllocLen = nPGMSNDROMLen - 0x600000;\n"
        "\t\tbprintf(PRINT_IMPORTANT, _T(\"[FBNeo] PS3 KOV2 compact sound: logical=%x physical=%x saved=%x\\n\"),\n"
        "\t\t\tnPGMSNDROMLen, nPGMSNDROMAllocLen, nPGMSNDROMLen - nPGMSNDROMAllocLen);\n"
        "\t}\n"
        "\tICSSNDROM\t\t= (UINT8*)BurnMalloc(nPGMSNDROMAllocLen);\n"
        "#else\n"
        "\tICSSNDROM\t\t= (UINT8*)BurnMalloc(nPGMSNDROMLen);\n"
        "#endif\n",
        "pgm_run: ICSSNDROM allocation",
    )

    text = replace_once(
        text,
        "\tics2115_init(ics2115_sound_irq, ICSSNDROM, nPGMSNDROMLen);\n",
        "\tics2115_init(ics2115_sound_irq, ICSSNDROM, nPGMSNDROMLen);\n"
        "#ifdef __PS3__\n"
        "\tif (pgm_ps3_kov2_compact_sound()) {\n"
        "\t\tics2115_set_rom_hole(0x200000, 0x800000);\n"
        "\t}\n"
        "#endif\n",
        "pgm_run: ics2115_init",
    )

    save(path, text, nl)
    print(f"patched: {path}")
    return True


def patch_ics_header(path: Path):
    text, nl = load(path)
    if MARKER in text:
        print(f"already patched: {path}")
        return False

    text = replace_once(
        text,
        "void ics2115_init(void(*cpu_irq_cb)(INT32), UINT8 *sample_rom, INT32 sample_rom_size);\n",
        "void ics2115_init(void(*cpu_irq_cb)(INT32), UINT8 *sample_rom, INT32 sample_rom_size);\n"
        "#ifdef __PS3__\n"
        f"// {MARKER}\n"
        "void ics2115_set_rom_hole(UINT32 start, UINT32 end);\n"
        "#endif\n",
        "ics2115.h: init declaration",
    )

    save(path, text, nl)
    print(f"patched: {path}")
    return True


def patch_ics_cpp(path: Path):
    text, nl = load(path)
    if MARKER in text:
        print(f"already patched: {path}")
        return False

    text = replace_once(
        text,
        "static UINT8* m_rom = NULL;\t\t\t// ics2115 rom\n"
        "static INT32 m_rom_mask;\n",
        "static UINT8* m_rom = NULL;\t\t\t// ics2115 rom\n"
        "static INT32 m_rom_mask;\n"
        "\n"
        "#ifdef __PS3__\n"
        f"// {MARKER}: logical hole removed from the physical sample-ROM allocation.\n"
        "static UINT32 m_rom_hole_start = 0;\n"
        "static UINT32 m_rom_hole_end = 0;\n"
        "\n"
        "void ics2115_set_rom_hole(UINT32 start, UINT32 end)\n"
        "{\n"
        "\tm_rom_hole_start = start;\n"
        "\tm_rom_hole_end = (end > start) ? end : start;\n"
        "\tbprintf(PRINT_IMPORTANT, _T(\"[FBNeo] ICS2115 compact ROM hole: %x-%x (%x bytes)\\n\"),\n"
        "\t\tm_rom_hole_start, m_rom_hole_end, m_rom_hole_end - m_rom_hole_start);\n"
        "}\n"
        "#endif\n"
        "\n"
        "static inline UINT8 ics2115_rom_read(UINT32 address)\n"
        "{\n"
        "\taddress &= m_rom_mask;\n"
        "#ifdef __PS3__\n"
        "\tif (m_rom_hole_end > m_rom_hole_start) {\n"
        "\t\tif (address >= m_rom_hole_start && address < m_rom_hole_end) return 0;\n"
        "\t\tif (address >= m_rom_hole_end) address -= (m_rom_hole_end - m_rom_hole_start);\n"
        "\t}\n"
        "#endif\n"
        "\treturn m_rom[address];\n"
        "}\n",
        "ics2115.cpp: ROM globals",
    )

    text = replace_once(
        text,
        "\tm_rom = sample_rom;\n\tm_rom_mask = sample_rom_size - 1;\n",
        "\tm_rom = sample_rom;\n\tm_rom_mask = sample_rom_size - 1;\n"
        "#ifdef __PS3__\n"
        "\t// Default to ordinary contiguous ROM. PGM enables the KOV2 hole after init.\n"
        "\tm_rom_hole_start = 0;\n"
        "\tm_rom_hole_end = 0;\n"
        "#endif\n",
        "ics2115.cpp: init ROM state",
    )

    # Replace the four direct sample-ROM reads in read_wavetable().
    old = (
        "static inline INT32 read_wavetable(ics2115_voice& voice, const UINT32 curr_addr)\n"
        "{\n"
        "\tif (voice.osc_conf.bitflags.ulaw || voice.osc_conf.bitflags.eightbit)\n"
        "\t{\n"
        "\t\tif (voice.osc_conf.bitflags.ulaw)\n"
        "\t\t\treturn m_ulaw[m_rom[curr_addr & m_rom_mask]];\n"
        "\n"
        "\t\treturn ((INT8)(m_rom[curr_addr & m_rom_mask]) << 8) | ((m_rom[curr_addr & m_rom_mask] & 0x7F) << 1);\n"
        "\t}\n"
        "\n"
        "\treturn ((INT8)(m_rom[(curr_addr + 1) & m_rom_mask]) << 8) | m_rom[(curr_addr + 0) & m_rom_mask];\n"
        "}\n"
    )
    new = (
        "static inline INT32 read_wavetable(ics2115_voice& voice, const UINT32 curr_addr)\n"
        "{\n"
        "\tif (voice.osc_conf.bitflags.ulaw || voice.osc_conf.bitflags.eightbit)\n"
        "\t{\n"
        "\t\tconst UINT8 sample = ics2115_rom_read(curr_addr);\n"
        "\t\tif (voice.osc_conf.bitflags.ulaw)\n"
        "\t\t\treturn m_ulaw[sample];\n"
        "\n"
        "\t\treturn ((INT8)sample << 8) | ((sample & 0x7F) << 1);\n"
        "\t}\n"
        "\n"
        "\treturn ((INT8)ics2115_rom_read(curr_addr + 1) << 8) | ics2115_rom_read(curr_addr + 0);\n"
        "}\n"
    )
    text = replace_once(text, old, new, "ics2115.cpp: read_wavetable")

    text = replace_once(
        text,
        "\tm_rom = NULL;\n\tm_rom_mask = 0;\n",
        "\tm_rom = NULL;\n\tm_rom_mask = 0;\n"
        "#ifdef __PS3__\n"
        "\tm_rom_hole_start = 0;\n"
        "\tm_rom_hole_end = 0;\n"
        "#endif\n",
        "ics2115.cpp: exit ROM state",
    )

    save(path, text, nl)
    print(f"patched: {path}")
    return True


def patch_draw_diag(path: Path):
    text, nl = load(path)
    marker = "PGM lazy draw-buffer allocation failed: draw=%p prio=%p screen=%p"
    if marker in text:
        print(f"already patched diagnostic: {path}")
        return False

    old = "\t\t\tbprintf(PRINT_ERROR, _T(\"[FBNeo] PGM lazy draw-buffer allocation failed.\\n\"));\n"
    new = (
        "\t\t\tbprintf(PRINT_ERROR, _T(\"[FBNeo] PGM lazy draw-buffer allocation failed: "
        "draw=%p prio=%p screen=%p\\n\"), (void*)pTempDraw, (void*)SpritePrio, (void*)pTempScreen);\n"
    )
    text = replace_once(text, old, new, "pgm_draw: lazy allocation diagnostic")
    save(path, text, nl)
    print(f"patched diagnostic: {path}")
    return True


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path.cwd().resolve()

    pgm_run = root / "src/burn/drv/pgm/pgm_run.cpp"
    pgm_draw = root / "src/burn/drv/pgm/pgm_draw.cpp"
    ics_h = root / "src/burn/snd/ics2115.h"
    ics_cpp = root / "src/burn/snd/ics2115.cpp"

    print(f"repo root: {root}")
    changed = False
    changed |= patch_pgm_run(pgm_run)
    changed |= patch_ics_header(ics_h)
    changed |= patch_ics_cpp(ics_cpp)
    changed |= patch_draw_diag(pgm_draw)

    print("\nResult:", "changes applied" if changed else "already fully patched")
    print("Expected PS3 KOV2 log after rebuild:")
    print("  [FBNeo] PS3 KOV2 compact sound: logical=1000000 physical=a00000 saved=600000")
    print("  [FBNeo] ICS2115 compact ROM hole: 200000-800000 (600000 bytes)")
    print("\nNon-PS3 builds and non-KOV2 PGM games keep the original layout.")


if __name__ == "__main__":
    main()
