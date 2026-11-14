// Included after the unchanged interpreter instruction functions and execute_one.
// CV1000 samples put roughly half of interpreter fallback steps in 0x60xx.
#ifndef FBNEO_SH3_INTERPRETER_HOT_H
#define FBNEO_SH3_INTERPRETER_HOT_H
#if defined(_MSC_VER)
#define SH3_HOT_INLINE __forceinline
#elif defined(__GNUC__)
#define SH3_HOT_INLINE inline __attribute__((always_inline))
#else
#define SH3_HOT_INLINE inline
#endif
static SH3_HOT_INLINE void sh3_execute_hot_fallback(const UINT16 opcode)
{
#if defined(SH3_PPC_DRC) || defined(SH3_HOT_FALLBACK_TEST)
	switch (opcode & 0xf000) {
		case 0x1000:
			MOVLS4(opcode);
			return;
		case 0x2000:
			switch (opcode & 0x0f) {
				case 0x00: MOVBS(opcode); return;
				case 0x01: MOVWS(opcode); return;
				case 0x02: MOVLS(opcode); return;
				case 0x04: MOVBM(opcode); return;
				case 0x05: MOVWM(opcode); return;
				case 0x06: MOVLM(opcode); return;
			}
			break;
		case 0x5000:
			MOVLL4(opcode);
			return;
		case 0x6000:
			switch (opcode & 0x0f) {
				case 0x00: MOVBL(opcode); return;
				case 0x01: MOVWL(opcode); return;
				case 0x02: MOVLL(opcode); return;
				case 0x03: MOV(opcode); return;
				case 0x04: MOVBP(opcode); return;
				case 0x05: MOVWP(opcode); return;
				case 0x06: MOVLP(opcode); return;
				case 0x07: NOT(opcode); return;
				case 0x08: SWAPB(opcode); return;
				case 0x09: SWAPW(opcode); return;
				case 0x0a: NEGC(opcode); return;
				case 0x0b: NEG(opcode); return;
				case 0x0c: EXTUB(opcode); return;
				case 0x0d: EXTUW(opcode); return;
				case 0x0e: EXTSB(opcode); return;
				case 0x0f: EXTSW(opcode); return;
			}
			break;
		case 0x7000:
			ADDI(opcode);
			return;
		case 0x9000:
			MOVWI(opcode);
			return;
		case 0xd000:
			MOVLI(opcode);
			return;
		case 0xe000:
			MOVI(opcode);
			return;
	}
#endif
	execute_one(opcode);
}
#undef SH3_HOT_INLINE
#endif
