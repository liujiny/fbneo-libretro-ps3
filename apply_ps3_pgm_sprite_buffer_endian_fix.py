#!/usr/bin/env python3
from pathlib import Path
import re, sys

root = Path.cwd()

def die(msg):
    print(f"[ERROR] {msg}", file=sys.stderr)
    sys.exit(1)

def find_file(*rels):
    for rel in rels:
        p = root / rel
        if p.exists():
            return p
    return None

draw = find_file(
    Path("src/burn/drv/pgm/pgm_draw.cpp"),
    Path("burn/drv/pgm/pgm_draw.cpp"),
)
run = find_file(
    Path("src/burn/drv/pgm/pgm_run.cpp"),
    Path("burn/drv/pgm/pgm_run.cpp"),
)

if draw is None or run is None:
    die("PGM source files not found. Run from project root or FBNeo source root.")

# ----------------------------------------------------------------------
# 1) Revert previous KOV-only prio0 diagnostic if present.
# ----------------------------------------------------------------------
sd = draw.read_text(encoding="utf-8")

prio_diag = '''	if (nBurnLayer & 2) {
		if (!OldCodeMode) {
#ifdef __PS3__
			// KOV PS3 diagnostic:
			// force low-priority sprites into the final frame. If this restores
			// the ship hull, pgm_video_control bit 0x2000 is being interpreted
			// incorrectly on the PS3 path rather than sprite graphics being bad.
			if (!strcmp(BurnDrvGetTextA(DRV_NAME), "kov")) {
				copy_sprite_priority(0);
			} else
#endif
			if ((pgm_video_control & 0x2000) == 0)
				copy_sprite_priority(0);
		} else {copy_sprite_priority(0);}
	}
'''

prio_orig = '''	if (nBurnLayer & 2) {
		if (!OldCodeMode) {
			if ((pgm_video_control & 0x2000) == 0)
				copy_sprite_priority(0);
		} else {copy_sprite_priority(0);}
	}
'''

if "KOV PS3 diagnostic:" in sd:
    if prio_diag not in sd:
        die("prio0 diagnostic marker found but block differs; refusing partial revert.")
    sd = sd.replace(prio_diag, prio_orig, 1)
    print("[OK] Reverted KOV force-prio0 diagnostic")
else:
    print("[INFO] KOV force-prio0 diagnostic not present")

# ----------------------------------------------------------------------
# 2) Fix sprite descriptor parsing in pgm_drawsprites().
#    Use logical endian-correct words once, including the terminator test.
# ----------------------------------------------------------------------
old_parse = '''	while (source < finish)
	{
		if (!OldCodeMode) {
			if ((source[4] & 0x7fff) == 0) break;	// verified on hardware
		} else {
			if (source[4] == 0) break;				// right?
		}

		INT32 xpos =  BURN_ENDIAN_SWAP_INT16(source[0]) & 0x07ff;
		INT32 ypos =  BURN_ENDIAN_SWAP_INT16(source[1]) & 0x03ff;
		INT32 xzom = (BURN_ENDIAN_SWAP_INT16(source[0]) & 0x7800) >> 11;
		INT32 xgrow= (BURN_ENDIAN_SWAP_INT16(source[0]) & 0x8000) >> 15;
		INT32 yzom = (BURN_ENDIAN_SWAP_INT16(source[1]) & 0x7800) >> 11;
		INT32 ygrow= (BURN_ENDIAN_SWAP_INT16(source[1]) & 0x8000) >> 15;
		INT32 palt = (BURN_ENDIAN_SWAP_INT16(source[2]) & 0x1f00) >> 8;
		INT32 flip = (BURN_ENDIAN_SWAP_INT16(source[2]) & 0x6000) >> 13;
		INT32 boff =((BURN_ENDIAN_SWAP_INT16(source[2]) & 0x007f) << 16) | (BURN_ENDIAN_SWAP_INT16(source[3]) & 0xffff);
		INT32 wide = (BURN_ENDIAN_SWAP_INT16(source[4]) & 0x7e00) >> 9;
		INT32 prio = (BURN_ENDIAN_SWAP_INT16(source[2]) & 0x0080) >> 7;
		INT32 high =  BURN_ENDIAN_SWAP_INT16(source[4]) & 0x01ff;

		if ((0 != nPGMSpriteBufferHack) || (OldCodeMode)) {
			if (source[2] & 0x8000) boff += 0x800000; // Real hardware does not have this! Useful for some rom hacks.
		}
'''

new_parse = '''	while (source < finish)
	{
		const UINT16 s0 = BURN_ENDIAN_SWAP_INT16(source[0]);
		const UINT16 s1 = BURN_ENDIAN_SWAP_INT16(source[1]);
		const UINT16 s2 = BURN_ENDIAN_SWAP_INT16(source[2]);
		const UINT16 s3 = BURN_ENDIAN_SWAP_INT16(source[3]);
		const UINT16 s4 = BURN_ENDIAN_SWAP_INT16(source[4]);

		if (!OldCodeMode) {
			if ((s4 & 0x7fff) == 0) break;	// verified on hardware
		} else {
			if (s4 == 0) break;				// right?
		}

		INT32 xpos =  s0 & 0x07ff;
		INT32 ypos =  s1 & 0x03ff;
		INT32 xzom = (s0 & 0x7800) >> 11;
		INT32 xgrow= (s0 & 0x8000) >> 15;
		INT32 yzom = (s1 & 0x7800) >> 11;
		INT32 ygrow= (s1 & 0x8000) >> 15;
		INT32 palt = (s2 & 0x1f00) >> 8;
		INT32 flip = (s2 & 0x6000) >> 13;
		INT32 boff =((s2 & 0x007f) << 16) | (s3 & 0xffff);
		INT32 wide = (s4 & 0x7e00) >> 9;
		INT32 prio = (s2 & 0x0080) >> 7;
		INT32 high =  s4 & 0x01ff;

		if ((0 != nPGMSpriteBufferHack) || (OldCodeMode)) {
			if (s2 & 0x8000) boff += 0x800000; // Real hardware does not have this! Useful for some rom hacks.
		}
'''

if "const UINT16 s0 = BURN_ENDIAN_SWAP_INT16(source[0]);" in sd:
    print("[SKIP] Endian-correct pgm_drawsprites parser already present")
else:
    if old_parse not in sd:
        die("Expected pgm_drawsprites() parser block not found.")
    sd = sd.replace(old_parse, new_parse, 1)
    print("[OK] Fixed endian-correct sprite descriptor parsing")

draw.write_text(sd, encoding="utf-8", newline="\n")

# ----------------------------------------------------------------------
# 3) Fix hardware masking in pgm_sprite_buffer().
#    Apply mask to logical 68K words, then store back in the same byte order.
# ----------------------------------------------------------------------
sr = run.read_text(encoding="utf-8")

old_buffer = '''static void pgm_sprite_buffer()
{
	if (pgm_video_control & 0x0001) // verified
	{
		UINT16 *ram16 = (UINT16*)PGM68KRAM;

		UINT16 mask[2][5] = {
			{ 0xffff, 0xfbff, 0x7fff, 0xffff, 0xffff }, // The sprite buffer hardware masks these bits!
			{ 0xffff, 0xffff, 0xffff, 0xffff, 0xffff }	// Some hacks rely on poor emulation
		};

		for (INT32 i = 0; i < 0xa00/2; i+= 10/2)
		{
			for (INT32 j = 0; j < 10 / 2; j++)
			{
				PGMSprBuf[(i / (10 / 2)) * (16 / 2) + j] = ram16[i + j] & mask[nPGMSpriteBufferHack][j];
			}

			if ((ram16[i+4] & 0x7fff) == 0) break; // verified on hardware
		}
	}
}
'''

new_buffer = '''static void pgm_sprite_buffer()
{
	if (pgm_video_control & 0x0001) // verified
	{
		UINT16 *ram16 = (UINT16*)PGM68KRAM;

		UINT16 mask[2][5] = {
			{ 0xffff, 0xfbff, 0x7fff, 0xffff, 0xffff }, // The sprite buffer hardware masks these bits!
			{ 0xffff, 0xffff, 0xffff, 0xffff, 0xffff }	// Some hacks rely on poor emulation
		};

		for (INT32 i = 0; i < 0xa00/2; i+= 10/2)
		{
			for (INT32 j = 0; j < 10 / 2; j++)
			{
				// The mask values describe logical 68K bits. On a big-endian
				// host such as PS3, ram16[] is still in the emulator's stored
				// byte order, so masking it directly clears the wrong bits.
				UINT16 word = BURN_ENDIAN_SWAP_INT16(ram16[i + j]);
				word &= mask[nPGMSpriteBufferHack][j];
				PGMSprBuf[(i / (10 / 2)) * (16 / 2) + j] =
					BURN_ENDIAN_SWAP_INT16(word);
			}

			const UINT16 sizeword = BURN_ENDIAN_SWAP_INT16(ram16[i + 4]);
			if ((sizeword & 0x7fff) == 0) break; // verified on hardware
		}
	}
}
'''

if "The mask values describe logical 68K bits." in sr:
    print("[SKIP] Endian-correct pgm_sprite_buffer already present")
else:
    if old_buffer not in sr:
        die("Expected pgm_sprite_buffer() block not found.")
    sr = sr.replace(old_buffer, new_buffer, 1)
    print("[OK] Fixed endian-correct sprite-buffer hardware masking")

run.write_text(sr, encoding="utf-8", newline="\n")

# Final validation
sd2 = draw.read_text(encoding="utf-8")
sr2 = run.read_text(encoding="utf-8")

checks = [
    ("draw parser", "const UINT16 s4 = BURN_ENDIAN_SWAP_INT16(source[4]);" in sd2),
    ("draw terminator", "if ((s4 & 0x7fff) == 0) break;" in sd2),
    ("buffer logical mask", "word &= mask[nPGMSpriteBufferHack][j];" in sr2),
    ("buffer swap-back", "BURN_ENDIAN_SWAP_INT16(word);" in sr2),
    ("buffer terminator", "const UINT16 sizeword = BURN_ENDIAN_SWAP_INT16(ram16[i + 4]);" in sr2),
]
bad = [name for name, ok in checks if not ok]
if bad:
    die("Validation failed: " + ", ".join(bad))

print("")
print("[OK] Applied complete PGM big-endian sprite descriptor fix")
print("     pgm_sprite_buffer(): mask logical 68K bits, not raw host words")
print("     pgm_drawsprites(): endian-correct terminator and descriptor parsing")
print("     Previous KOV prio0 diagnostic: reverted")
print("")
print("Inspect:")
print("  git diff -- src/burn/drv/pgm/pgm_draw.cpp src/burn/drv/pgm/pgm_run.cpp")
