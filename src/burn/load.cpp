// Burn - Rom Loading module
#include "burnint.h"

// Load a rom and separate out the bytes by nGap
// Dest is the memory block to insert the rom into
#if defined(__PS3__)
#define PS3_ROM_CHUNK_SIZE (64 * 1024)

static INT32 BurnLoadRomChunked(UINT8 *Dest, INT32 i, INT32 nLen, INT32 nGap, INT32 nFlags)
{
	INT32 nGroup = (LD_GROUP(nFlags) > 0) ? LD_GROUP(nFlags) : 1;
	INT32 nInvert = (nFlags & LD_INVERT) ? 0xff : 0;
	INT32 nByteswap = (nFlags & LD_BYTESWAP) ? 1 : 0;
	INT32 nReverse = (nGroup > 1) ? (nFlags & LD_REVERSE) : 0;
	INT32 nXor = (nFlags & LD_XOR) ? 1 : 0;
	INT32 nNibbles = (nFlags & LD_NIBBLES) ? 1 : 0;
	INT32 nNibblesHiLo = (nFlags & (LD_HI_NIBBLE | LD_LO_NIBBLE)) ? 1 : 0;

	if (nNibbles) { nGroup = 1; nGap = 2; }
	// Byte-swap indexes pairs. Never split a group/pair across chunks.
	INT32 alignment = nGroup;
	if (nByteswap && alignment < 2) alignment = 2;
	INT32 chunkSize = PS3_ROM_CHUNK_SIZE - (PS3_ROM_CHUNK_SIZE % alignment);
	UINT8 *load = (UINT8*)BurnMalloc(chunkSize);
	if (load == NULL) return 1;

	INT32 result = 0;
	for (INT32 offset = 0; offset < nLen; offset += chunkSize) {
		INT32 wanted = nLen - offset;
		if (wanted > chunkSize) wanted = chunkSize;
		INT32 loaded = 0;
		memset(load, 0, wanted);
		if (BurnExtLoadRomChunk(load, &loaded, i, (UINT32)offset, (UINT32)wanted) != 0 || loaded != wanted) {
			// Unsupported backend (notably solid 7z): request the legacy loader
			// only if no destination bytes have been touched yet.
			result = (offset == 0) ? 2 : 1;
			break;
		}

		for (INT32 n = 0; n < loaded; n += nGroup) {
			INT32 z = ((offset + n) / nGroup) * nGap;
			if (nNibbles) {
				Dest[z + 0] = (load[n ^ nByteswap] ^ nInvert) & 0x0f;
				Dest[z + 1] = (load[n ^ nByteswap] ^ nInvert) >> 4;
			} else if (nNibblesHiLo) {
				if (nFlags & LD_HI_NIBBLE)
					Dest[z] = (Dest[z] & 0x0f) | ((load[n ^ nByteswap] ^ nInvert) << 4);
				else
					Dest[z] = (Dest[z] & 0xf0) | ((load[n ^ nByteswap] ^ nInvert) & 0x0f);
			} else {
				INT32 available = loaded - n;
				if (available > nGroup) available = nGroup;
				for (INT32 j = 0; j < available; j++) {
					INT32 source = nReverse ? n + (nGroup - 1 - j) : n + j;
					source ^= nByteswap;
					if (source >= loaded) { result = 1; break; }
					INT32 x = nInvert;
					if (nXor) x ^= Dest[z + j];
					Dest[z + j] = load[source] ^ x;
				}
				if (result) break;
			}
		}
		if (result) break;
	}

	BurnFree(load);
	return result;
}
#endif
INT32 BurnLoadRomExt(UINT8 *Dest, INT32 i, INT32 nGap, INT32 nFlags)
{
	INT32 nRet = 0, nLen = 0;
	struct BurnRomInfo ri;

	if (BurnExtLoadRom == NULL) return 1; // Load function was not defined by the application

	// Find the length of the rom (as given by the current driver)
	{
		ri.nType = 0;
		ri.nLen = 0;
		BurnDrvGetRomInfo(&ri,i);
		if (ri.nType == 0) return 0; // Empty rom slot - don't load anything and return success
		nLen = ri.nLen;
	}

	char* RomName = ""; //added by emufan
	BurnDrvGetRomName(&RomName, i, 0);

	if (nLen <= 0) return 1;

#if defined(__PS3__)
	if (!bDoIpsPatch && BurnExtLoadRomChunk != NULL &&
		((nGap > 1) || (nFlags & LD_NIBBLES) || (nFlags & LD_XOR) ||
		 (nFlags & (LD_HI_NIBBLE | LD_LO_NIBBLE)))) {
		INT32 chunked = BurnLoadRomChunked(Dest, i, nLen, nGap, nFlags);
		if (chunked != 2) return chunked;
		// Backend cannot seek/decode in chunks; retain the compatible full-buffer path.
	}
#endif

	if ((nGap>1) || (nFlags & LD_NIBBLES) || (nFlags & LD_XOR) || (nFlags & (LD_HI_NIBBLE | LD_LO_NIBBLE)))
	{
		// Use temporary memory to load ROM, ips patching is also done here, enough space must be reserved.
		if (bDoIpsPatch) {
			if (0 == nIpsMemExpLen[EXP_FLAG]) {			// Unspecified nIpsMemExpLen[LOAD_ROM].
				IpsApplyPatches(NULL, RomName, ri.nCrc, true);	// Get the maximum offset of ips.
				if (nIpsMemExpLen[LOAD_ROM] > nLen) {	// ips offset is greater than rom length.
					nLen = nIpsMemExpLen[LOAD_ROM];
				}
			} else {									// Customized nIpsMemExpLen[LOAD_ROM].
				nLen += nIpsMemExpLen[LOAD_ROM];
			}
		}

		INT32 nLoadLen = 0;
		UINT8* Load = (UINT8*)BurnMalloc(nLen);
		if (Load == NULL) return 1;
		memset(Load, 0, nLen);

		// Load in the file
		nRet = BurnExtLoadRom(Load, &nLoadLen, i);
		if (bDoIpsPatch) IpsApplyPatches(Load, RomName, ri.nCrc);
		if (nRet != 0) { if (Load) { BurnFree(Load); Load = NULL; } return 1; }

		if (nLoadLen < 0) nLoadLen = 0;
		if (nLoadLen > nLen || bDoIpsPatch) nLoadLen = nLen;

		INT32 nGroup = (LD_GROUP(nFlags) > 0) ? LD_GROUP(nFlags) : 1;
		INT32 nInvert = (nFlags & LD_INVERT) ? 0xff : 0;
		INT32 nByteswap = (nFlags & LD_BYTESWAP) ? 1 : 0;
		INT32 nReverse = (nGroup > 1) ? (nFlags & LD_REVERSE) : 0;
		INT32 nXor = (nFlags & LD_XOR) ? 1 : 0;
		INT32 nNibbles = (nFlags & LD_NIBBLES) ? 1 : 0;
		INT32 nNibblesHiLo = (nFlags & (LD_HI_NIBBLE | LD_LO_NIBBLE)) ? 1 : 0;
		UINT8 *Src = Load;

		if (nNibbles) { nGroup = 1; nGap = 2; }

		for (INT32 n = 0, z = 0; n < nLoadLen; n += nGroup, z += nGap) {
			if (nNibbles)
			{
				Dest[z + 0] = (Src[n ^ nByteswap] ^ nInvert) & 0xf;
				Dest[z + 1] = (Src[n ^ nByteswap] ^ nInvert) >> 4;
			}
			else if (nNibblesHiLo)
			{
				if (nFlags & LD_HI_NIBBLE) {
					Dest[z] = (Dest[z] & 0x0f) | ((Src[n ^ nByteswap] ^ nInvert) << 4);
				} else {
					Dest[z] = (Dest[z] & 0xf0) | ((Src[n ^ nByteswap] ^ nInvert) & 0xf);
				}
			}
			else
			{
				if (nReverse) {
					for (INT32 j = 0; j < nGroup; j++) {
						INT32 nXorData = nInvert;
						if (nXor) nXorData ^= Dest[z + j];
						Dest[z + j] = Src[(n + ((nGroup - 1) - j)) ^ nByteswap] ^ nXorData;
					}
				} else {
					for (INT32 j = 0; j < nGroup; j++) {
						INT32 nXorData = nInvert;
						if (nXor) nXorData ^= Dest[z + j];
						Dest[z + j] = Src[(n + j) ^ nByteswap] ^ nXorData;
					}
				}
			}
		}

		if (Load) {
			BurnFree(Load);
			Load = NULL;
		}
	} else {
 		// If no XOR, and gap of 1, just copy straight in
		nRet = BurnExtLoadRom(Dest, NULL, i);
		if (bDoIpsPatch) {
			IpsApplyPatches(NULL, RomName, ri.nCrc, true);	// Get the maximum offset of ips. & megadrive needs.
			IpsApplyPatches(Dest, RomName, ri.nCrc);
		}
		if (nRet != 0) return 1;

		if (nFlags & LD_INVERT) {
			for (INT32 n = 0; n < nLen; n++) {
				Dest[n] ^= 0xff;
			}
		}

		if (nFlags & LD_BYTESWAP) {
			BurnByteswap(Dest, nLen);
		}
	}

	return 0;
}

INT32 BurnLoadRom(UINT8 *Dest, INT32 i, INT32 nGap)
{
	return BurnLoadRomExt(Dest, i, nGap, 0);
}

INT32 BurnXorRom(UINT8 *Dest, INT32 i, INT32 nGap)
{
	return BurnLoadRomExt(Dest, i, nGap, LD_XOR);
}

// Separate out a bitfield into Bit number 'nField' of each nibble in pDest
// (end result: each dword in memory carries the 8 pixels of a tile line).
INT32 BurnLoadBitField(UINT8 *pDest, UINT8 *pSrc, INT32 nField, INT32 nSrcLen)
{
	INT32 nPix = 0;

	for (nPix = 0; nPix < (nSrcLen << 3); nPix++)
	{
		INT32 nBit;
		// Get the bitplane pixel value (on or off)
		nBit = (*pSrc) >> (7 - (nPix & 7)); nBit &= 1;
		nBit<<=nField; // Move to correct bit for this field

		// use low nibble for each even pixel
		if ((nPix & 1) == 1) nBit <<= 4; // use high nibble for each odd pixel

		*pDest|=nBit; // OR into destination
		if ((nPix & 1) == 1) pDest++;
		if ((nPix & 7) == 7) pSrc++;
  	}

	return 0;
}

