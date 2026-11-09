#include "../gen/sh3_types.h"
#include <stdio.h>
#include <stdlib.h>

static inline int32_t sign_extend_8(uint32_t val) {
    return (val & 0x80) ? ((int32_t)val - 0x100) : (int32_t)val;
}

static inline int32_t sign_extend_12(uint32_t val) {
    return (val & 0x800) ? ((int32_t)val - 0x1000) : (int32_t)val;
}

static void sh3_exec_insn(Sh3VM *vm, uint32_t inst_pc, uint16_t op, bool *branched, uint32_t *target_pc) {
    uint32_t top = (op >> 12) & 0xf;
    uint32_t n = (op >> 8) & 0xf;
    uint32_t m = (op >> 4) & 0xf;
    uint32_t d4 = op & 0xf;
    uint32_t d8 = op & 0xff;
    int32_t  i8 = sign_extend_8(d8);
    int32_t  disp12 = sign_extend_12(op & 0xfff);

    vm->cycles -= 1;

    switch (top) {
        case 0x0: {
            if (op == 0x0009) { /* nop */ }
            else if (op == 0x0008) { vm->t = 0; }
            else if (op == 0x0018) { vm->t = 1; }
            else if (op == 0x0019) { vm->sr &= ~0x300; vm->t = 0; }
            else if (op == 0x001b) { if (vm->cycles > 0) vm->cycles = 0; }
            else if (op == 0x0028) { vm->mach = 0; vm->macl = 0; }
            else if (op == 0x000b) { /* rts */
                if (branched) {
                    *branched = true;
                    uint32_t ret_pc = vm->pr;
                    uint16_t delay_op = cv1k_read_w_pc(vm, inst_pc + 2);
                    sh3_exec_insn(vm, inst_pc + 2, delay_op, NULL, NULL);
                    *target_pc = ret_pc;
                }
            }
            else if (op == 0x002b) { /* rte */
                if (branched) {
                    *branched = true;
                    uint32_t ret_pc = vm->spc;
                    vm->sr = vm->ssr;
                    vm->t = vm->sr & 1;
                    uint16_t delay_op = cv1k_read_w_pc(vm, inst_pc + 2);
                    sh3_exec_insn(vm, inst_pc + 2, delay_op, NULL, NULL);
                    *target_pc = ret_pc;
                }
            }
            else if ((op & 0xf0ff) == 0x0002) { vm->r[n] = (vm->sr & ~1) | (vm->t & 1); }
            else if ((op & 0xf0ff) == 0x0003) { /* bsrf Rn */
                if (branched) {
                    uint32_t tgt = inst_pc + 4 + vm->r[n];
                    vm->pr = inst_pc + 4;
                    *branched = true;
                    uint16_t delay_op = cv1k_read_w_pc(vm, inst_pc + 2);
                    sh3_exec_insn(vm, inst_pc + 2, delay_op, NULL, NULL);
                    *target_pc = tgt;
                }
            }
            else if ((op & 0xf0ff) == 0x000a) { vm->r[n] = vm->mach; }
            else if ((op & 0xf0ff) == 0x0012) { vm->r[n] = vm->gbr; }
            else if ((op & 0xf0ff) == 0x001a) { vm->r[n] = vm->macl; }
            else if ((op & 0xf0ff) == 0x0022) { vm->r[n] = vm->vbr; }
            else if ((op & 0xf0ff) == 0x0023) { /* braf Rn */
                if (branched) {
                    uint32_t tgt = inst_pc + 4 + vm->r[n];
                    *branched = true;
                    uint16_t delay_op = cv1k_read_w_pc(vm, inst_pc + 2);
                    sh3_exec_insn(vm, inst_pc + 2, delay_op, NULL, NULL);
                    *target_pc = tgt;
                }
            }
            else if ((op & 0xf0ff) == 0x0029) { vm->r[n] = vm->t; }
            else if ((op & 0xf0ff) == 0x002a) { vm->r[n] = vm->pr; }
            else if ((op & 0xf0ff) == 0x0032) { vm->r[n] = vm->ssr; }
            else if ((op & 0xf0ff) == 0x0042) { vm->r[n] = vm->spc; }
            else if ((op & 0xf00f) == 0x0004) { WRITE_B(vm->r[0] + vm->r[n], (uint8_t)vm->r[m]); }
            else if ((op & 0xf00f) == 0x0005) { WRITE_W(vm->r[0] + vm->r[n], (uint16_t)vm->r[m]); }
            else if ((op & 0xf00f) == 0x0006) { WRITE_L(vm->r[0] + vm->r[n], vm->r[m]); }
            else if ((op & 0xf00f) == 0x0007) { vm->macl = (uint32_t)((uint64_t)vm->r[m] * (uint64_t)vm->r[n]); }
            else if ((op & 0xf00f) == 0x000c) { vm->r[n] = (int32_t)(int8_t)READ_B(vm->r[0] + vm->r[m]); }
            else if ((op & 0xf00f) == 0x000d) { vm->r[n] = (int32_t)(int16_t)READ_W(vm->r[0] + vm->r[m]); }
            else if ((op & 0xf00f) == 0x000e) { vm->r[n] = READ_L(vm->r[0] + vm->r[m]); }
            else if ((op & 0xf00f) == 0x000f) {
                int64_t mul = (int64_t)(int32_t)READ_L(vm->r[n]) * (int64_t)(int32_t)READ_L(vm->r[m]);
                vm->r[n] += 4; vm->r[m] += 4;
                int64_t acc = ((int64_t)vm->mach << 32) | vm->macl;
                acc += mul;
                vm->mach = (uint32_t)(acc >> 32); vm->macl = (uint32_t)acc;
            }
            break;
        }

        case 0x1:
            WRITE_L(vm->r[n] + d4 * 4, vm->r[m]);
            break;

        case 0x2:
            if (d4 == 0) WRITE_B(vm->r[n], (uint8_t)vm->r[m]);
            else if (d4 == 1) WRITE_W(vm->r[n], (uint16_t)vm->r[m]);
            else if (d4 == 2) WRITE_L(vm->r[n], vm->r[m]);
            else if (d4 == 4) { vm->r[n] -= 1; WRITE_B(vm->r[n], (uint8_t)vm->r[m]); }
            else if (d4 == 5) { vm->r[n] -= 2; WRITE_W(vm->r[n], (uint16_t)vm->r[m]); }
            else if (d4 == 6) { vm->r[n] -= 4; WRITE_L(vm->r[n], vm->r[m]); }
            else if (d4 == 7) {
                vm->sr = (vm->sr & ~0x300) | ((vm->r[n] >> 23) & 0x100) | ((vm->r[m] >> 22) & 0x200);
                vm->t = ((vm->r[n] ^ vm->r[m]) >> 31) & 1;
            }
            else if (d4 == 8) { vm->t = (vm->r[n] & vm->r[m]) == 0; }
            else if (d4 == 9) { vm->r[n] &= vm->r[m]; }
            else if (d4 == 0xa) { vm->r[n] ^= vm->r[m]; }
            else if (d4 == 0xb) { vm->r[n] |= vm->r[m]; }
            else if (d4 == 0xc) {
                uint32_t diff = vm->r[n] ^ vm->r[m];
                vm->t = !((diff & 0xff000000) && (diff & 0x00ff0000) && (diff & 0x0000ff00) && (diff & 0x000000ff));
            }
            else if (d4 == 0xd) { vm->r[n] = (vm->r[n] >> 16) | (vm->r[m] << 16); }
            else if (d4 == 0xe) { vm->macl = (uint32_t)(uint16_t)vm->r[n] * (uint32_t)(uint16_t)vm->r[m]; }
            else if (d4 == 0xf) { vm->macl = (uint32_t)((int32_t)(int16_t)vm->r[n] * (int32_t)(int16_t)vm->r[m]); }
            break;

        case 0x3:
            if (d4 == 0) vm->t = (vm->r[n] == vm->r[m]);
            else if (d4 == 2) vm->t = ((uint32_t)vm->r[n] >= (uint32_t)vm->r[m]);
            else if (d4 == 3) vm->t = ((int32_t)vm->r[n] >= (int32_t)vm->r[m]);
            else if (d4 == 5) {
                uint64_t res = (uint64_t)vm->r[n] * (uint64_t)vm->r[m];
                vm->mach = (uint32_t)(res >> 32); vm->macl = (uint32_t)res;
            }
            else if (d4 == 6) vm->t = ((uint32_t)vm->r[n] > (uint32_t)vm->r[m]);
            else if (d4 == 7) vm->t = ((int32_t)vm->r[n] > (int32_t)vm->r[m]);
            else if (d4 == 8) vm->r[n] -= vm->r[m];
            else if (d4 == 0xa) {
                uint32_t tmp0 = vm->r[n];
                uint32_t tmp1 = tmp0 - vm->r[m];
                vm->r[n] = tmp1 - vm->t;
                vm->t = (tmp0 < tmp1) || (tmp1 < vm->r[n]);
            }
            else if (d4 == 0xc) vm->r[n] += vm->r[m];
            else if (d4 == 0xd) {
                int64_t res = (int64_t)(int32_t)vm->r[n] * (int64_t)(int32_t)vm->r[m];
                vm->mach = (uint32_t)(res >> 32); vm->macl = (uint32_t)res;
            }
            else if (d4 == 0xe) {
                uint32_t tmp0 = vm->r[n];
                uint32_t tmp1 = tmp0 + vm->r[m];
                vm->r[n] = tmp1 + vm->t;
                vm->t = (tmp1 < tmp0) || (vm->r[n] < tmp1);
            }
            break;

        case 0x4: {
            uint32_t sub = op & 0xff;
            if (sub == 0x00) { vm->t = (vm->r[n] >> 31) & 1; vm->r[n] <<= 1; }
            else if (sub == 0x01) { vm->t = vm->r[n] & 1; vm->r[n] >>= 1; }
            else if (sub == 0x02) { vm->r[n] -= 4; WRITE_L(vm->r[n], vm->mach); }
            else if (sub == 0x03) { vm->r[n] -= 4; WRITE_L(vm->r[n], (vm->sr & ~1) | (vm->t & 1)); }
            else if (sub == 0x04) { vm->t = (vm->r[n] >> 31) & 1; vm->r[n] = (vm->r[n] << 1) | vm->t; }
            else if (sub == 0x05) { vm->t = vm->r[n] & 1; vm->r[n] = (vm->r[n] >> 1) | (vm->t << 31); }
            else if (sub == 0x06) { vm->mach = READ_L(vm->r[n]); vm->r[n] += 4; }
            else if (sub == 0x07) { vm->sr = READ_L(vm->r[n]); vm->t = vm->sr & 1; vm->r[n] += 4; }
            else if (sub == 0x08) { vm->r[n] <<= 2; }
            else if (sub == 0x09) { vm->r[n] >>= 2; }
            else if (sub == 0x0a) { vm->mach = vm->r[n]; }
            else if (sub == 0x0b) { /* jsr @Rn */
                if (branched) {
                    uint32_t tgt = vm->r[n];
                    vm->pr = inst_pc + 4;
                    *branched = true;
                    uint16_t delay_op = cv1k_read_w_pc(vm, inst_pc + 2);
                    sh3_exec_insn(vm, inst_pc + 2, delay_op, NULL, NULL);
                    *target_pc = tgt;
                }
            }
            else if (sub == 0x0e) { vm->sr = vm->r[n]; vm->t = vm->sr & 1; }
            else if (sub == 0x10) { vm->r[n]--; vm->t = (vm->r[n] == 0); }
            else if (sub == 0x11) { vm->t = ((int32_t)vm->r[n] >= 0); }
            else if (sub == 0x12) { vm->r[n] -= 4; WRITE_L(vm->r[n], vm->macl); }
            else if (sub == 0x13) { vm->r[n] -= 4; WRITE_L(vm->r[n], vm->gbr); }
            else if (sub == 0x15) { vm->t = ((int32_t)vm->r[n] > 0); }
            else if (sub == 0x16) { vm->macl = READ_L(vm->r[n]); vm->r[n] += 4; }
            else if (sub == 0x17) { vm->gbr = READ_L(vm->r[n]); vm->r[n] += 4; }
            else if (sub == 0x18) { vm->r[n] <<= 8; }
            else if (sub == 0x19) { vm->r[n] >>= 8; }
            else if (sub == 0x1a) { vm->macl = vm->r[n]; }
            else if (sub == 0x1e) { vm->gbr = vm->r[n]; }
            else if (sub == 0x20) { vm->t = (vm->r[n] >> 31) & 1; vm->r[n] <<= 1; }
            else if (sub == 0x21) { vm->t = vm->r[n] & 1; vm->r[n] = (uint32_t)((int32_t)vm->r[n] >> 1); }
            else if (sub == 0x22) { vm->r[n] -= 4; WRITE_L(vm->r[n], vm->pr); }
            else if (sub == 0x23) { vm->r[n] -= 4; WRITE_L(vm->r[n], vm->vbr); }
            else if (sub == 0x24) { uint32_t tmp = (vm->r[n] >> 31) & 1; vm->r[n] = (vm->r[n] << 1) | vm->t; vm->t = tmp; }
            else if (sub == 0x25) { uint32_t tmp = vm->r[n] & 1; vm->r[n] = (vm->r[n] >> 1) | (vm->t << 31); vm->t = tmp; }
            else if (sub == 0x26) { vm->pr = READ_L(vm->r[n]); vm->r[n] += 4; }
            else if (sub == 0x27) { vm->vbr = READ_L(vm->r[n]); vm->r[n] += 4; }
            else if (sub == 0x28) { vm->r[n] <<= 16; }
            else if (sub == 0x29) { vm->r[n] >>= 16; }
            else if (sub == 0x2a) { vm->pr = vm->r[n]; }
            else if (sub == 0x2b) { /* jmp @Rn */
                if (branched) {
                    uint32_t tgt = vm->r[n];
                    *branched = true;
                    uint16_t delay_op = cv1k_read_w_pc(vm, inst_pc + 2);
                    sh3_exec_insn(vm, inst_pc + 2, delay_op, NULL, NULL);
                    *target_pc = tgt;
                }
            }
            else if (sub == 0x2e) { vm->vbr = vm->r[n]; }
            else if (sub == 0x33) { vm->r[n] -= 4; WRITE_L(vm->r[n], vm->ssr); }
            else if (sub == 0x43) { vm->r[n] -= 4; WRITE_L(vm->r[n], vm->spc); }
            else if (sub == 0x37) { vm->ssr = READ_L(vm->r[n]); vm->r[n] += 4; }
            else if (sub == 0x47) { vm->spc = READ_L(vm->r[n]); vm->r[n] += 4; }
            else if (sub == 0x3e) { vm->ssr = vm->r[n]; }
            else if (sub == 0x4e) { vm->spc = vm->r[n]; }
            else if ((op & 0xf00f) == 0x400c) { /* shad */
                int s = (int32_t)vm->r[m];
                vm->r[n] = (s >= 0) ? (vm->r[n] << (s & 31)) : ((s <= -32) ? ((int32_t)vm->r[n] >> 31) : ((int32_t)vm->r[n] >> (-s & 31)));
            }
            else if ((op & 0xf00f) == 0x400d) { /* shld */
                int s = (int32_t)vm->r[m];
                vm->r[n] = (s >= 0) ? (vm->r[n] << (s & 31)) : ((s <= -32) ? 0 : (vm->r[n] >> (-s & 31)));
            }
            else if ((op & 0xf00f) == 0x400f) { /* mac.w @Rm+, @Rn+ */
                int32_t mul = (int32_t)(int16_t)READ_W(vm->r[n]) * (int32_t)(int16_t)READ_W(vm->r[m]);
                vm->r[n] += 2; vm->r[m] += 2;
                int64_t acc = ((int64_t)vm->mach << 32) | vm->macl;
                acc += mul;
                vm->mach = (uint32_t)(acc >> 32); vm->macl = (uint32_t)acc;
            }
            break;
        }

        case 0x5:
            vm->r[n] = READ_L(vm->r[m] + d4 * 4);
            break;

        case 0x6:
            if (d4 == 0) vm->r[n] = (int32_t)(int8_t)READ_B(vm->r[m]);
            else if (d4 == 1) vm->r[n] = (int32_t)(int16_t)READ_W(vm->r[m]);
            else if (d4 == 2) vm->r[n] = READ_L(vm->r[m]);
            else if (d4 == 3) vm->r[n] = vm->r[m];
            else if (d4 == 4) { vm->r[n] = (int32_t)(int8_t)READ_B(vm->r[m]); vm->r[m] += 1; }
            else if (d4 == 5) { vm->r[n] = (int32_t)(int16_t)READ_W(vm->r[m]); vm->r[m] += 2; }
            else if (d4 == 6) { vm->r[n] = READ_L(vm->r[m]); vm->r[m] += 4; }
            else if (d4 == 7) vm->r[n] = ~vm->r[m];
            else if (d4 == 8) vm->r[n] = (vm->r[m] & 0xffff0000) | ((vm->r[m] & 0xff) << 8) | ((vm->r[m] >> 8) & 0xff);
            else if (d4 == 9) vm->r[n] = (vm->r[m] >> 16) | (vm->r[m] << 16);
            else if (d4 == 0xa) {
                uint32_t tmp = 0 - vm->r[m];
                vm->r[n] = tmp - vm->t;
                vm->t = (0 < tmp) || (tmp < vm->r[n]);
            }
            else if (d4 == 0xb) vm->r[n] = -vm->r[m];
            else if (d4 == 0xc) vm->r[n] = (uint8_t)vm->r[m];
            else if (d4 == 0xd) vm->r[n] = (uint16_t)vm->r[m];
            else if (d4 == 0xe) vm->r[n] = (int32_t)(int8_t)vm->r[m];
            else if (d4 == 0xf) vm->r[n] = (int32_t)(int16_t)vm->r[m];
            break;

        case 0x7:
            vm->r[n] += i8;
            break;

        case 0x8: {
            uint32_t sub = (op >> 8) & 0xf;
            if (sub == 0) WRITE_B(vm->r[m] + d4, (uint8_t)vm->r[0]);
            else if (sub == 1) WRITE_W(vm->r[m] + d4 * 2, (uint16_t)vm->r[0]);
            else if (sub == 4) vm->r[0] = (int32_t)(int8_t)READ_B(vm->r[m] + d4);
            else if (sub == 5) vm->r[0] = (int32_t)(int16_t)READ_W(vm->r[m] + d4 * 2);
            else if (sub == 8) vm->t = (vm->r[0] == (uint32_t)i8);
            else if (sub == 9) { /* bt disp */
                if (branched) {
                    *branched = true;
                    *target_pc = vm->t ? (inst_pc + 4 + i8 * 2) : (inst_pc + 2);
                }
            }
            else if (sub == 0xb) { /* bf disp */
                if (branched) {
                    *branched = true;
                    *target_pc = (!vm->t) ? (inst_pc + 4 + i8 * 2) : (inst_pc + 2);
                }
            }
            else if (sub == 0xd) { /* bt/s disp */
                if (branched) {
                    *branched = true;
                    if (vm->t) {
                        uint16_t delay_op = cv1k_read_w_pc(vm, inst_pc + 2);
                        sh3_exec_insn(vm, inst_pc + 2, delay_op, NULL, NULL);
                        *target_pc = inst_pc + 4 + i8 * 2;
                    } else {
                        *target_pc = inst_pc + 4;
                    }
                }
            }
            else if (sub == 0xf) { /* bf/s disp */
                if (branched) {
                    *branched = true;
                    if (!vm->t) {
                        uint16_t delay_op = cv1k_read_w_pc(vm, inst_pc + 2);
                        sh3_exec_insn(vm, inst_pc + 2, delay_op, NULL, NULL);
                        *target_pc = inst_pc + 4 + i8 * 2;
                    } else {
                        *target_pc = inst_pc + 4;
                    }
                }
            }
            break;
        }

        case 0x9:
            vm->r[n] = (int32_t)(int16_t)READ_W_PC(inst_pc + 4 + d8 * 2);
            break;

        case 0xa: /* bra disp */
            if (branched) {
                *branched = true;
                uint16_t delay_op = cv1k_read_w_pc(vm, inst_pc + 2);
                sh3_exec_insn(vm, inst_pc + 2, delay_op, NULL, NULL);
                *target_pc = inst_pc + 4 + disp12 * 2;
            }
            break;

        case 0xb: /* bsr disp */
            if (branched) {
                vm->pr = inst_pc + 4;
                *branched = true;
                uint16_t delay_op = cv1k_read_w_pc(vm, inst_pc + 2);
                sh3_exec_insn(vm, inst_pc + 2, delay_op, NULL, NULL);
                *target_pc = inst_pc + 4 + disp12 * 2;
            }
            break;

        case 0xc: {
            uint32_t sub = (op >> 8) & 0xf;
            if (sub == 0) WRITE_B(vm->gbr + d8, (uint8_t)vm->r[0]);
            else if (sub == 1) WRITE_W(vm->gbr + d8 * 2, (uint16_t)vm->r[0]);
            else if (sub == 2) WRITE_L(vm->gbr + d8 * 4, vm->r[0]);
            else if (sub == 3) { /* trapa */ }
            else if (sub == 4) vm->r[0] = (int32_t)(int8_t)READ_B(vm->gbr + d8);
            else if (sub == 5) vm->r[0] = (int32_t)(int16_t)READ_W(vm->gbr + d8 * 2);
            else if (sub == 6) vm->r[0] = READ_L(vm->gbr + d8 * 4);
            else if (sub == 7) vm->r[0] = ((inst_pc & ~3) + 4) + d8 * 4;
            else if (sub == 8) vm->t = (vm->r[0] & d8) == 0;
            else if (sub == 9) vm->r[0] &= d8;
            else if (sub == 0xa) vm->r[0] ^= d8;
            else if (sub == 0xb) vm->r[0] |= d8;
            break;
        }

        case 0xd:
            vm->r[n] = READ_L_PC(((inst_pc & ~3) + 4) + d8 * 4);
            break;

        case 0xe:
            vm->r[n] = (uint32_t)i8;
            break;

        default:
            break;
    }
}

void sh3_interpret_block(Sh3VM *vm) {
    uint32_t pc = vm->pc;
    int max_insns = 128;

    while (max_insns-- > 0 && vm->cycles > 0) {
        uint16_t op = cv1k_read_w_pc(vm, pc);
        bool branched = false;
        uint32_t target_pc = 0;

        uint32_t inst_pc = pc;
        sh3_exec_insn(vm, inst_pc, op, &branched, &target_pc);

        if (branched) {
            vm->pc = target_pc;
            return;
        }

        pc += 2;

        /* If we reached an address with a compiled native block, return to dispatch */
        Sh3BlockFn next = sh3_find_block(pc);
        if (next != NULL) {
            vm->pc = pc;
            return;
        }
    }

    vm->pc = pc;
}
