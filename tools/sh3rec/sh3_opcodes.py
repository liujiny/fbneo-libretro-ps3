"""
SH-3 Instruction Decoder & C Code Generator for CV1000 Static Recompilation.
Translates SH-3 16-bit opcodes into highly optimized native C statements.
"""

class DecodedInsn:
    def __init__(self, pc, op, mnemonic, c_code, cycles=1, is_branch=False, is_delay_slot=False, 
                 branch_target=None, is_indirect=False, is_call=False, is_return=False):
        self.pc = pc
        self.op = op
        self.mnemonic = mnemonic
        self.c_code = c_code
        self.cycles = cycles
        self.is_branch = is_branch
        self.is_delay_slot = is_delay_slot
        self.branch_target = branch_target
        self.is_indirect = is_indirect
        self.is_call = is_call
        self.is_return = is_return

def sign_extend_8(val):
    return val - 0x100 if val & 0x80 else val

def sign_extend_12(val):
    return val - 0x1000 if val & 0x800 else val

def decode_insn(pc, op, rom_data=None):
    n = (op >> 8) & 0xf
    m = (op >> 4) & 0xf
    d4 = op & 0xf
    d8 = op & 0xff
    i8 = sign_extend_8(d8)
    d12 = op & 0xfff
    disp12 = sign_extend_12(d12)

    top = (op >> 12) & 0xf

    # Default values
    is_branch = False
    is_delay_slot = False
    branch_target = None
    is_indirect = False
    is_call = False
    is_return = False
    cycles = 1

    # 0000 group
    if top == 0:
        if op == 0x0009:
            return DecodedInsn(pc, op, "nop", "; /* nop */", 1)
        elif op == 0x000b:
            return DecodedInsn(pc, op, "rts", "/* rts target in pr */", 2, 
                               is_branch=True, is_delay_slot=True, is_return=True, is_indirect=True)
        elif op == 0x0008:
            return DecodedInsn(pc, op, "clrt", "t = 0;", 1)
        elif op == 0x0018:
            return DecodedInsn(pc, op, "sett", "t = 1;", 1)
        elif op == 0x0019:
            return DecodedInsn(pc, op, "div0u", "sr &= ~(0x00000300); t = 0;", 1)
        elif op == 0x001b:
            return DecodedInsn(pc, op, "sleep", "if (vm->cycles > 0) vm->cycles = 0;", 1)
        elif op == 0x0028:
            return DecodedInsn(pc, op, "clrmac", "mach = 0; macl = 0;", 1)
        elif op == 0x002b:
            return DecodedInsn(pc, op, "rte", "/* rte */", 4, is_branch=True, is_delay_slot=True, is_return=True)
        elif (op & 0xf0ff) == 0x0002:
            return DecodedInsn(pc, op, f"stc sr, r{n}", f"r{n} = (sr & ~1) | (t & 1);", 1)
        elif (op & 0xf0ff) == 0x0003: # BSRF Rn
            return DecodedInsn(pc, op, f"bsrf r{n}", f"pr = 0x{pc+4:08x}; /* bsrf */", 2,
                               is_branch=True, is_delay_slot=True, is_call=True, is_indirect=True)
        elif (op & 0xf0ff) == 0x000a:
            return DecodedInsn(pc, op, f"sts mach, r{n}", f"r{n} = mach;", 1)
        elif (op & 0xf0ff) == 0x0012:
            return DecodedInsn(pc, op, f"stc gbr, r{n}", f"r{n} = gbr;", 1)
        elif (op & 0xf0ff) == 0x001a:
            return DecodedInsn(pc, op, f"sts macl, r{n}", f"r{n} = macl;", 1)
        elif (op & 0xf0ff) == 0x0022:
            return DecodedInsn(pc, op, f"stc vbr, r{n}", f"r{n} = vbr;", 1)
        elif (op & 0xf0ff) == 0x0023: # BRAF Rn
            return DecodedInsn(pc, op, f"braf r{n}", "/* braf */", 2,
                               is_branch=True, is_delay_slot=True, is_indirect=True)
        elif (op & 0xf0ff) == 0x0029:
            return DecodedInsn(pc, op, f"movt r{n}", f"r{n} = t;", 1)
        elif (op & 0xf0ff) == 0x002a:
            return DecodedInsn(pc, op, f"sts pr, r{n}", f"r{n} = pr;", 1)
        elif (op & 0xf0ff) == 0x0032:
            return DecodedInsn(pc, op, f"stc ssr, r{n}", f"r{n} = vm->ssr;", 1)
        elif (op & 0xf0ff) == 0x0042:
            return DecodedInsn(pc, op, f"stc spc, r{n}", f"r{n} = vm->spc;", 1)
        elif (op & 0xf00f) == 0x0004:
            return DecodedInsn(pc, op, f"mov.b r{m}, @(r0, r{n})", f"WRITE_B(r0 + r{n}, (uint8_t)r{m});", 1)
        elif (op & 0xf00f) == 0x0005:
            return DecodedInsn(pc, op, f"mov.w r{m}, @(r0, r{n})", f"WRITE_W(r0 + r{n}, (uint16_t)r{m});", 1)
        elif (op & 0xf00f) == 0x0006:
            return DecodedInsn(pc, op, f"mov.l r{m}, @(r0, r{n})", f"WRITE_L(r0 + r{n}, r{m});", 1)
        elif (op & 0xf00f) == 0x0007:
            return DecodedInsn(pc, op, f"mul.l r{m}, r{n}", f"macl = (uint32_t)((uint64_t)(uint32_t)r{m} * (uint64_t)(uint32_t)r{n});", 2)
        elif (op & 0xf00f) == 0x000c:
            return DecodedInsn(pc, op, f"mov.b @(r0, r{m}), r{n}", f"r{n} = (int32_t)(int8_t)READ_B(r0 + r{m});", 1)
        elif (op & 0xf00f) == 0x000d:
            return DecodedInsn(pc, op, f"mov.w @(r0, r{m}), r{n}", f"r{n} = (int32_t)(int16_t)READ_W(r0 + r{m});", 1)
        elif (op & 0xf00f) == 0x000e:
            return DecodedInsn(pc, op, f"mov.l @(r0, r{m}), r{n}", f"r{n} = READ_L(r0 + r{m});", 1)
        elif (op & 0xf00f) == 0x000f: # mac.l @Rm+, @Rn+
            return DecodedInsn(pc, op, f"mac.l @r{m}+, @r{n}+",
                               f"{{ int64_t mul = (int64_t)(int32_t)READ_L(r{n}) * (int64_t)(int32_t)READ_L(r{m}); r{n} += 4; r{m} += 4; int64_t acc = ((int64_t)mach << 32) | macl; acc += mul; mach = (uint32_t)(acc >> 32); macl = (uint32_t)acc; }}", 3)

    # 1nnn group: mov.l Rm, @(disp*4, Rn)
    elif top == 1:
        disp = d4 * 4
        return DecodedInsn(pc, op, f"mov.l r{m}, @(0x{disp:x}, r{n})", f"WRITE_L(r{n} + 0x{disp:x}, r{m});", 1)

    # 2nnn group
    elif top == 2:
        if d4 == 0:
            return DecodedInsn(pc, op, f"mov.b r{m}, @r{n}", f"WRITE_B(r{n}, (uint8_t)r{m});", 1)
        elif d4 == 1:
            return DecodedInsn(pc, op, f"mov.w r{m}, @r{n}", f"WRITE_W(r{n}, (uint16_t)r{m});", 1)
        elif d4 == 2:
            return DecodedInsn(pc, op, f"mov.l r{m}, @r{n}", f"WRITE_L(r{n}, r{m});", 1)
        elif d4 == 4:
            return DecodedInsn(pc, op, f"mov.b r{m}, @-r{n}", f"r{n} -= 1; WRITE_B(r{n}, (uint8_t)r{m});", 1)
        elif d4 == 5:
            return DecodedInsn(pc, op, f"mov.w r{m}, @-r{n}", f"r{n} -= 2; WRITE_W(r{n}, (uint16_t)r{m});", 1)
        elif d4 == 6:
            return DecodedInsn(pc, op, f"mov.l r{m}, @-r{n}", f"r{n} -= 4; WRITE_L(r{n}, r{m});", 1)
        elif d4 == 7: # div0s
            return DecodedInsn(pc, op, f"div0s r{m}, r{n}", 
                               f"sr = (sr & ~0x300) | ((r{n} >> 23) & 0x100) | ((r{m} >> 22) & 0x200); t = ((r{n} ^ r{m}) >> 31) & 1;", 1)
        elif d4 == 8: # tst
            return DecodedInsn(pc, op, f"tst r{m}, r{n}", f"t = (r{n} & r{m}) == 0;", 1)
        elif d4 == 9: # and
            return DecodedInsn(pc, op, f"and r{m}, r{n}", f"r{n} &= r{m};", 1)
        elif d4 == 0xa: # xor
            return DecodedInsn(pc, op, f"xor r{m}, r{n}", f"r{n} ^= r{m};", 1)
        elif d4 == 0xb: # or
            return DecodedInsn(pc, op, f"or r{m}, r{n}", f"r{n} |= r{m};", 1)
        elif d4 == 0xc: # cmp/str
            return DecodedInsn(pc, op, f"cmp/str r{m}, r{n}",
                               f"{{ uint32_t diff = r{n} ^ r{m}; t = !((diff & 0xff000000) && (diff & 0x00ff0000) && (diff & 0x0000ff00) && (diff & 0x000000ff)); }}", 1)
        elif d4 == 0xd: # xtrct
            return DecodedInsn(pc, op, f"xtrct r{m}, r{n}", f"r{n} = (r{n} >> 16) | (r{m} << 16);", 1)
        elif d4 == 0xe: # mulu.w
            return DecodedInsn(pc, op, f"mulu.w r{m}, r{n}", f"macl = (uint32_t)(uint16_t)r{n} * (uint32_t)(uint16_t)r{m};", 1)
        elif d4 == 0xf: # muls.w
            return DecodedInsn(pc, op, f"muls.w r{m}, r{n}", f"macl = (uint32_t)((int32_t)(int16_t)r{n} * (int32_t)(int16_t)r{m});", 1)

    # 3nnn group
    elif top == 3:
        if d4 == 0:
            return DecodedInsn(pc, op, f"cmp/eq r{m}, r{n}", f"t = (r{n} == r{m});", 1)
        elif d4 == 2:
            return DecodedInsn(pc, op, f"cmp/hs r{m}, r{n}", f"t = ((uint32_t)r{n} >= (uint32_t)r{m});", 1)
        elif d4 == 3:
            return DecodedInsn(pc, op, f"cmp/ge r{m}, r{n}", f"t = ((int32_t)r{n} >= (int32_t)r{m});", 1)
        elif d4 == 5: # dmulu.l
            return DecodedInsn(pc, op, f"dmulu.l r{m}, r{n}",
                               f"{{ uint64_t res = (uint64_t)r{n} * (uint64_t)r{m}; mach = (uint32_t)(res >> 32); macl = (uint32_t)res; }}", 2)
        elif d4 == 6:
            return DecodedInsn(pc, op, f"cmp/hi r{m}, r{n}", f"t = ((uint32_t)r{n} > (uint32_t)r{m});", 1)
        elif d4 == 7:
            return DecodedInsn(pc, op, f"cmp/gt r{m}, r{n}", f"t = ((int32_t)r{n} > (int32_t)r{m});", 1)
        elif d4 == 8:
            return DecodedInsn(pc, op, f"sub r{m}, r{n}", f"r{n} -= r{m};", 1)
        elif d4 == 0xa:
            return DecodedInsn(pc, op, f"subc r{m}, r{n}",
                               f"{{ uint32_t tmp0 = r{n}; uint32_t tmp1 = tmp0 - r{m}; r{n} = tmp1 - t; t = (tmp0 < tmp1) || (tmp1 < r{n}); }}", 1)
        elif d4 == 0xc:
            return DecodedInsn(pc, op, f"add r{m}, r{n}", f"r{n} += r{m};", 1)
        elif d4 == 0xd: # dmuls.l
            return DecodedInsn(pc, op, f"dmuls.l r{m}, r{n}",
                               f"{{ int64_t res = (int64_t)(int32_t)r{n} * (int64_t)(int32_t)r{m}; mach = (uint32_t)(res >> 32); macl = (uint32_t)res; }}", 2)
        elif d4 == 0xe:
            return DecodedInsn(pc, op, f"addc r{m}, r{n}",
                               f"{{ uint32_t tmp0 = r{n}; uint32_t tmp1 = tmp0 + r{m}; r{n} = tmp1 + t; t = (tmp1 < tmp0) || (r{n} < tmp1); }}", 1)

    # 4nnn group
    elif top == 4:
        sub = op & 0xff
        if sub == 0x00: return DecodedInsn(pc, op, f"shll r{n}", f"t = (r{n} >> 31) & 1; r{n} <<= 1;", 1)
        elif sub == 0x01: return DecodedInsn(pc, op, f"shlr r{n}", f"t = r{n} & 1; r{n} >>= 1;", 1)
        elif sub == 0x02: return DecodedInsn(pc, op, f"sts.l mach, @-r{n}", f"r{n} -= 4; WRITE_L(r{n}, mach);", 1)
        elif sub == 0x03: return DecodedInsn(pc, op, f"stc.l sr, @-r{n}", f"r{n} -= 4; WRITE_L(r{n}, (sr & ~1) | (t & 1));", 1)
        elif sub == 0x04: return DecodedInsn(pc, op, f"rotl r{n}", f"t = (r{n} >> 31) & 1; r{n} = (r{n} << 1) | t;", 1)
        elif sub == 0x05: return DecodedInsn(pc, op, f"rotr r{n}", f"t = r{n} & 1; r{n} = (r{n} >> 1) | (t << 31);", 1)
        elif sub == 0x06: return DecodedInsn(pc, op, f"lds.l @r{n}+, mach", f"mach = READ_L(r{n}); r{n} += 4;", 1)
        elif sub == 0x07: return DecodedInsn(pc, op, f"ldc.l @r{n}+, sr", f"sr = READ_L(r{n}); t = sr & 1; r{n} += 4;", 1)
        elif sub == 0x08: return DecodedInsn(pc, op, f"shll2 r{n}", f"r{n} <<= 2;", 1)
        elif sub == 0x09: return DecodedInsn(pc, op, f"shlr2 r{n}", f"r{n} >>= 2;", 1)
        elif sub == 0x0a: return DecodedInsn(pc, op, f"lds r{n}, mach", f"mach = r{n};", 1)
        elif sub == 0x0b: # JSR @Rn
            return DecodedInsn(pc, op, f"jsr @r{n}", f"pr = 0x{pc+4:08x}; /* jsr */", 2,
                               is_branch=True, is_delay_slot=True, is_call=True, is_indirect=True)
        elif sub == 0x0e: return DecodedInsn(pc, op, f"ldc r{n}, sr", f"sr = r{n}; t = sr & 1;", 1)
        elif sub == 0x10: return DecodedInsn(pc, op, f"dt r{n}", f"r{n}--; t = (r{n} == 0);", 1)
        elif sub == 0x11: return DecodedInsn(pc, op, f"cmp/pz r{n}", f"t = ((int32_t)r{n} >= 0);", 1)
        elif sub == 0x12: return DecodedInsn(pc, op, f"sts.l macl, @-r{n}", f"r{n} -= 4; WRITE_L(r{n}, macl);", 1)
        elif sub == 0x13: return DecodedInsn(pc, op, f"stc.l gbr, @-r{n}", f"r{n} -= 4; WRITE_L(r{n}, gbr);", 1)
        elif sub == 0x15: return DecodedInsn(pc, op, f"cmp/pl r{n}", f"t = ((int32_t)r{n} > 0);", 1)
        elif sub == 0x16: return DecodedInsn(pc, op, f"lds.l @r{n}+, macl", f"macl = READ_L(r{n}); r{n} += 4;", 1)
        elif sub == 0x17: return DecodedInsn(pc, op, f"ldc.l @r{n}+, gbr", f"gbr = READ_L(r{n}); r{n} += 4;", 1)
        elif sub == 0x18: return DecodedInsn(pc, op, f"shll8 r{n}", f"r{n} <<= 8;", 1)
        elif sub == 0x19: return DecodedInsn(pc, op, f"shlr8 r{n}", f"r{n} >>= 8;", 1)
        elif sub == 0x1a: return DecodedInsn(pc, op, f"lds r{n}, macl", f"macl = r{n};", 1)
        elif sub == 0x1e: return DecodedInsn(pc, op, f"ldc r{n}, gbr", f"gbr = r{n};", 1)
        elif sub == 0x20: return DecodedInsn(pc, op, f"shal r{n}", f"t = (r{n} >> 31) & 1; r{n} <<= 1;", 1)
        elif sub == 0x21: return DecodedInsn(pc, op, f"shar r{n}", f"t = r{n} & 1; r{n} = (uint32_t)((int32_t)r{n} >> 1);", 1)
        elif sub == 0x22: return DecodedInsn(pc, op, f"sts.l pr, @-r{n}", f"r{n} -= 4; WRITE_L(r{n}, pr);", 1)
        elif sub == 0x23: return DecodedInsn(pc, op, f"stc.l vbr, @-r{n}", f"r{n} -= 4; WRITE_L(r{n}, vbr);", 1)
        elif sub == 0x24: return DecodedInsn(pc, op, f"rotcl r{n}", f"{{ uint32_t tmp = (r{n} >> 31) & 1; r{n} = (r{n} << 1) | t; t = tmp; }}", 1)
        elif sub == 0x25: return DecodedInsn(pc, op, f"rotcr r{n}", f"{{ uint32_t tmp = r{n} & 1; r{n} = (r{n} >> 1) | (t << 31); t = tmp; }}", 1)
        elif sub == 0x26: return DecodedInsn(pc, op, f"lds.l @r{n}+, pr", f"pr = READ_L(r{n}); r{n} += 4;", 1)
        elif sub == 0x27: return DecodedInsn(pc, op, f"ldc.l @r{n}+, vbr", f"vbr = READ_L(r{n}); r{n} += 4;", 1)
        elif sub == 0x28: return DecodedInsn(pc, op, f"shll16 r{n}", f"r{n} <<= 16;", 1)
        elif sub == 0x29: return DecodedInsn(pc, op, f"shlr16 r{n}", f"r{n} >>= 16;", 1)
        elif sub == 0x2a: return DecodedInsn(pc, op, f"lds r{n}, pr", f"pr = r{n};", 1)
        elif sub == 0x2b: # JMP @Rn
            return DecodedInsn(pc, op, f"jmp @r{n}", "/* jmp */", 2,
                               is_branch=True, is_delay_slot=True, is_indirect=True)
        elif sub == 0x2e: return DecodedInsn(pc, op, f"ldc r{n}, vbr", f"vbr = r{n};", 1)
        elif sub == 0x33: return DecodedInsn(pc, op, f"stc.l ssr, @-r{n}", f"r{n} -= 4; WRITE_L(r{n}, vm->ssr);", 1)
        elif sub == 0x43: return DecodedInsn(pc, op, f"stc.l spc, @-r{n}", f"r{n} -= 4; WRITE_L(r{n}, vm->spc);", 1)
        elif sub == 0x37: return DecodedInsn(pc, op, f"ldc.l @r{n}+, ssr", f"vm->ssr = READ_L(r{n}); r{n} += 4;", 1)
        elif sub == 0x47: return DecodedInsn(pc, op, f"ldc.l @r{n}+, spc", f"vm->spc = READ_L(r{n}); r{n} += 4;", 1)
        elif sub == 0x3e: return DecodedInsn(pc, op, f"ldc r{n}, ssr", f"vm->ssr = r{n};", 1)
        elif sub == 0x4e: return DecodedInsn(pc, op, f"ldc r{n}, spc", f"vm->spc = r{n};", 1)
        elif (op & 0xf00f) == 0x400c: # shad
            return DecodedInsn(pc, op, f"shad r{m}, r{n}",
                               f"{{ int s = (int32_t)r{m}; r{n} = (s >= 0) ? (r{n} << (s & 31)) : ((s <= -32) ? ((int32_t)r{n} >> 31) : ((int32_t)r{n} >> (-s & 31))); }}", 1)
        elif (op & 0xf00f) == 0x400d: # shld
            return DecodedInsn(pc, op, f"shld r{m}, r{n}",
                               f"{{ int s = (int32_t)r{m}; r{n} = (s >= 0) ? (r{n} << (s & 31)) : ((s <= -32) ? 0 : (r{n} >> (-s & 31))); }}", 1)
        elif (op & 0xf00f) == 0x400f: # mac.w @Rm+, @Rn+
            return DecodedInsn(pc, op, f"mac.w @r{m}+, @r{n}+",
                               f"{{ int32_t mul = (int32_t)(int16_t)READ_W(r{n}) * (int32_t)(int16_t)READ_W(r{m}); r{n} += 2; r{m} += 2; int64_t acc = ((int64_t)mach << 32) | macl; acc += mul; mach = (uint32_t)(acc >> 32); macl = (uint32_t)acc; }}", 3)

    # 5nnn group: mov.l @(disp*4, Rm), Rn
    elif top == 5:
        disp = d4 * 4
        return DecodedInsn(pc, op, f"mov.l @(0x{disp:x}, r{m}), r{n}", f"r{n} = READ_L(r{m} + 0x{disp:x});", 1)

    # 6nnn group
    elif top == 6:
        if d4 == 0: return DecodedInsn(pc, op, f"mov.b @r{m}, r{n}", f"r{n} = (int32_t)(int8_t)READ_B(r{m});", 1)
        elif d4 == 1: return DecodedInsn(pc, op, f"mov.w @r{m}, r{n}", f"r{n} = (int32_t)(int16_t)READ_W(r{m});", 1)
        elif d4 == 2: return DecodedInsn(pc, op, f"mov.l @r{m}, r{n}", f"r{n} = READ_L(r{m});", 1)
        elif d4 == 3: return DecodedInsn(pc, op, f"mov r{m}, r{n}", f"r{n} = r{m};", 1)
        elif d4 == 4: return DecodedInsn(pc, op, f"mov.b @r{m}+, r{n}", f"r{n} = (int32_t)(int8_t)READ_B(r{m}); r{m} += 1;", 1)
        elif d4 == 5: return DecodedInsn(pc, op, f"mov.w @r{m}+, r{n}", f"r{n} = (int32_t)(int16_t)READ_W(r{m}); r{m} += 2;", 1)
        elif d4 == 6: return DecodedInsn(pc, op, f"mov.l @r{m}+, r{n}", f"r{n} = READ_L(r{m}); r{m} += 4;", 1)
        elif d4 == 7: return DecodedInsn(pc, op, f"not r{m}, r{n}", f"r{n} = ~r{m};", 1)
        elif d4 == 8: return DecodedInsn(pc, op, f"swap.b r{m}, r{n}", f"r{n} = (r{m} & 0xffff0000) | ((r{m} & 0xff) << 8) | ((r{m} >> 8) & 0xff);", 1)
        elif d4 == 9: return DecodedInsn(pc, op, f"swap.w r{m}, r{n}", f"r{n} = (r{m} >> 16) | (r{m} << 16);", 1)
        elif d4 == 0xa: return DecodedInsn(pc, op, f"negc r{m}, r{n}", f"{{ uint32_t tmp = 0 - r{m}; r{n} = tmp - t; t = (0 < tmp) || (tmp < r{n}); }}", 1)
        elif d4 == 0xb: return DecodedInsn(pc, op, f"neg r{m}, r{n}", f"r{n} = -r{m};", 1)
        elif d4 == 0xc: return DecodedInsn(pc, op, f"extu.b r{m}, r{n}", f"r{n} = (uint8_t)r{m};", 1)
        elif d4 == 0xd: return DecodedInsn(pc, op, f"extu.w r{m}, r{n}", f"r{n} = (uint16_t)r{m};", 1)
        elif d4 == 0xe: return DecodedInsn(pc, op, f"exts.b r{m}, r{n}", f"r{n} = (int32_t)(int8_t)r{m};", 1)
        elif d4 == 0xf: return DecodedInsn(pc, op, f"exts.w r{m}, r{n}", f"r{n} = (int32_t)(int16_t)r{m};", 1)

    # 7nnn group: add #imm, Rn
    elif top == 7:
        return DecodedInsn(pc, op, f"add #{i8}, r{n}", f"r{n} += {i8};", 1)

    # 8nnn group
    elif top == 8:
        sub = (op >> 8) & 0xf
        if sub == 0:
            return DecodedInsn(pc, op, f"mov.b r0, @(0x{d4:x}, r{m})", f"WRITE_B(r{m} + 0x{d4:x}, (uint8_t)r0);", 1)
        elif sub == 1:
            disp = d4 * 2
            return DecodedInsn(pc, op, f"mov.w r0, @(0x{disp:x}, r{m})", f"WRITE_W(r{m} + 0x{disp:x}, (uint16_t)r0);", 1)
        elif sub == 4:
            return DecodedInsn(pc, op, f"mov.b @(0x{d4:x}, r{m}), r0", f"r0 = (int32_t)(int8_t)READ_B(r{m} + 0x{d4:x});", 1)
        elif sub == 5:
            disp = d4 * 2
            return DecodedInsn(pc, op, f"mov.w @(0x{disp:x}, r{m}), r0", f"r0 = (int32_t)(int16_t)READ_W(r{m} + 0x{disp:x});", 1)
        elif sub == 8:
            return DecodedInsn(pc, op, f"cmp/eq #{i8}, r0", f"t = (r0 == {i8});", 1)
        elif sub == 9: # BT disp
            target = (pc + 4 + i8 * 2) & 0xffffffff
            return DecodedInsn(pc, op, f"bt 0x{target:08x}", "/* bt */", 1, is_branch=True, branch_target=target)
        elif sub == 0xb: # BF disp
            target = (pc + 4 + i8 * 2) & 0xffffffff
            return DecodedInsn(pc, op, f"bf 0x{target:08x}", "/* bf */", 1, is_branch=True, branch_target=target)
        elif sub == 0xd: # BT/S disp
            target = (pc + 4 + i8 * 2) & 0xffffffff
            return DecodedInsn(pc, op, f"bt/s 0x{target:08x}", "/* bt/s */", 1, is_branch=True, is_delay_slot=True, branch_target=target)
        elif sub == 0xf: # BF/S disp
            target = (pc + 4 + i8 * 2) & 0xffffffff
            return DecodedInsn(pc, op, f"bf/s 0x{target:08x}", "/* bf/s */", 1, is_branch=True, is_delay_slot=True, branch_target=target)

    # 9nnn group: mov.w @(disp*2, PC), Rn
    elif top == 9:
        target = (pc + 4 + d8 * 2) & 0xffffffff
        return DecodedInsn(pc, op, f"mov.w @(0x{target:x}), r{n}", f"r{n} = (int32_t)(int16_t)READ_W_PC(0x{target:08x});", 1)

    # Annn group: bra disp
    elif top == 0xa:
        target = (pc + 4 + disp12 * 2) & 0xffffffff
        return DecodedInsn(pc, op, f"bra 0x{target:08x}", "/* bra */", 2, is_branch=True, is_delay_slot=True, branch_target=target)

    # Bnnn group: bsr disp
    elif top == 0xb:
        target = (pc + 4 + disp12 * 2) & 0xffffffff
        return DecodedInsn(pc, op, f"bsr 0x{target:08x}", f"pr = 0x{pc+4:08x}; /* bsr */", 2, 
                           is_branch=True, is_delay_slot=True, is_call=True, branch_target=target)

    # Cnnn group
    elif top == 0xc:
        sub = (op >> 8) & 0xf
        if sub == 0: return DecodedInsn(pc, op, f"mov.b r0, @(0x{d8:x}, gbr)", f"WRITE_B(gbr + 0x{d8:x}, (uint8_t)r0);", 1)
        elif sub == 1: return DecodedInsn(pc, op, f"mov.w r0, @(0x{d8*2:x}, gbr)", f"WRITE_W(gbr + 0x{d8*2:x}, (uint16_t)r0);", 1)
        elif sub == 2: return DecodedInsn(pc, op, f"mov.l r0, @(0x{d8*4:x}, gbr)", f"WRITE_L(gbr + 0x{d8*4:x}, r0);", 1)
        elif sub == 3: return DecodedInsn(pc, op, f"trapa #{d8}", f"/* trapa #{d8} */", 8)
        elif sub == 4: return DecodedInsn(pc, op, f"mov.b @(0x{d8:x}, gbr), r0", f"r0 = (int32_t)(int8_t)READ_B(gbr + 0x{d8:x});", 1)
        elif sub == 5: return DecodedInsn(pc, op, f"mov.w @(0x{d8*2:x}, gbr), r0", f"r0 = (int32_t)(int16_t)READ_W(gbr + 0x{d8*2:x});", 1)
        elif sub == 6: return DecodedInsn(pc, op, f"mov.l @(0x{d8*4:x}, gbr), r0", f"r0 = READ_L(gbr + 0x{d8*4:x});", 1)
        elif sub == 7: # MOVA
            target = (pc & ~3) + 4 + d8 * 4
            return DecodedInsn(pc, op, f"mova @(0x{target:x}), r0", f"r0 = 0x{target:08x};", 1)
        elif sub == 8: return DecodedInsn(pc, op, f"tst #{d8}, r0", f"t = (r0 & 0x{d8:02x}) == 0;", 1)
        elif sub == 9: return DecodedInsn(pc, op, f"and #{d8}, r0", f"r0 &= 0x{d8:02x};", 1)
        elif sub == 0xa: return DecodedInsn(pc, op, f"xor #{d8}, r0", f"r0 ^= 0x{d8:02x};", 1)
        elif sub == 0xb: return DecodedInsn(pc, op, f"or #{d8}, r0", f"r0 |= 0x{d8:02x};", 1)

    # Dnnn group: mov.l @(disp*4, PC), Rn
    elif top == 0xd:
        target = (pc & ~3) + 4 + d8 * 4
        return DecodedInsn(pc, op, f"mov.l @(0x{target:x}), r{n}", f"r{n} = READ_L_PC(0x{target:08x});", 1)

    # Ennn group: mov #imm, Rn
    elif top == 0xe:
        return DecodedInsn(pc, op, f"mov #{i8}, r{n}", f"r{n} = {i8};", 1)

    return DecodedInsn(pc, op, f"unknown_0x{op:04x}", f"/* unknown opcode 0x{op:04x} */", 1)
