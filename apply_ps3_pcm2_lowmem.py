#!/usr/bin/env python3
from pathlib import Path
import re, sys

root = Path.cwd()

def die(msg):
    print(f"[ERROR] {msg}", file=sys.stderr)
    sys.exit(1)

candidates = [
    root / "src" / "burn" / "drv" / "neogeo" / "d_neogeo.cpp",
    root / "burn" / "drv" / "neogeo" / "d_neogeo.cpp",
]
target = next((x for x in candidates if x.exists()), None)

if target is None:
    die("d_neogeo.cpp not found. Run this from either the project root or the FBNeo source root.")

s = target.read_text(encoding="utf-8")

if "PS3 low-memory in-place PCM2 V2 decrypt" in s:
    print("[SKIP] PS3 low-memory PCM2 V2 patch already present")
    sys.exit(0)

pat = re.compile(
    r"static void PCM2DecryptV2\(const PCM2DecryptV2Info\* const pInfo\)\s*\n"
    r"\{\s*\n"
    r"\s*// Decrypt V-ROMs\s*\n"
    r".*?"
    r"\n\}\s*\n"
    r"\nstatic void PCM2DecryptP2",
    re.S
)

if not pat.search(s):
    die("PCM2DecryptV2() function block not found.")

replacement = r'''static void PCM2DecryptV2(const PCM2DecryptV2Info* const pInfo)
{
	// Decrypt V-ROMs

#if defined(__PS3__) && defined(__PSL1GHT__)
	/*
	 * PS3 low-memory in-place PCM2 V2 decrypt.
	 *
	 * The generic path duplicates the entire 16 MiB V-ROM with
	 * BurnMalloc(0x01000000). On PS3 this can fail late in Neo Geo init.
	 * The old code then silently skips decryption, leaving mslug5 with
	 * valid video but noise.
	 *
	 * The transform is a permutation of the 24-bit V-ROM address space,
	 * so it can be applied in place by walking permutation cycles.
	 * A 1-bit-per-byte visited map costs 2 MiB instead of a 16 MiB copy.
	 */
	const UINT32 nSize = 0x01000000;
	const UINT32 nMask = nSize - 1;
	UINT8* pVisited = (UINT8*)BurnMalloc(nSize >> 3);

	if (pVisited) {
		UINT8* pRom = YM2610ADPCMAROM[nNeoActiveSlot];
		memset(pVisited, 0, nSize >> 3);

		for (UINT32 nStart = 0; nStart < nSize; nStart++) {
			const UINT8 nVisitedMask = (UINT8)(1U << (nStart & 7));

			if (pVisited[nStart >> 3] & nVisitedMask) {
				continue;
			}

			UINT32 nCurrent = nStart;
			UINT8 nCarry = pRom[nCurrent];

			do {
				const UINT32 i =
					(nCurrent - (UINT32)pInfo->nAddressXor) & nMask;
				const UINT32 nAddress =
					(((i & 0x00FEFFFE) |
					  ((i & 0x00010000) >> 16) |
					  ((i & 0x00000001) << 16)) ^
					 (UINT32)pInfo->nAddressOffset) & nMask;

				const UINT8 nNextCarry = pRom[nAddress];

				pRom[nAddress] =
					nCarry ^ pInfo->nDataXor[nAddress & 0x07];

				pVisited[nCurrent >> 3] |=
					(UINT8)(1U << (nCurrent & 7));

				nCurrent = nAddress;
				nCarry = nNextCarry;
			} while (nCurrent != nStart);
		}

		BurnFree(pVisited);
	}
#else
	UINT8* pTemp = (UINT8*)BurnMalloc(0x01000000);

	if (pTemp) {
		memcpy(pTemp, YM2610ADPCMAROM[nNeoActiveSlot], 0x01000000);

		for (INT32 i = 0; i < 0x01000000; i++) {
			INT32 nAddress = ((i & 0x00FEFFFE) | ((i & 0x00010000) >> 16) | ((i & 0x00000001) << 16)) ^ pInfo->nAddressOffset;

			YM2610ADPCMAROM[nNeoActiveSlot][nAddress] = pTemp[(i + pInfo->nAddressXor) & 0xffffff] ^ pInfo->nDataXor[nAddress & 0x07];
		}

		BurnFree(pTemp);
	}
#endif
}

static void PCM2DecryptP2'''

s2, n = pat.subn(replacement, s, count=1)
if n != 1:
    die(f"Expected exactly one replacement, got {n}")

target.write_text(s2, encoding="utf-8", newline="\n")

check = target.read_text(encoding="utf-8")
required = [
    "PS3 low-memory in-place PCM2 V2 decrypt",
    "BurnMalloc(nSize >> 3)",
    "#if defined(__PS3__) && defined(__PSL1GHT__)",
    "#else",
    "BurnMalloc(0x01000000)",
]
missing = [x for x in required if x not in check]
if missing:
    die("Validation failed: " + ", ".join(missing))

print("[OK] Applied PS3 low-memory PCM2 V2 decrypt")
print("     PS3 temporary memory: 16 MiB -> 2 MiB")
print("     Sony SDK and non-PSL1GHT paths unchanged")
print("Run:")
print("  git diff -- src/burn/drv/neogeo/d_neogeo.cpp")
