#!/usr/bin/env python3
"""
SH-3 Static Recompiler Code Emitter for Cave CV1000 on PS3.
Translates both ROM and initial decompressed RAM into fast, native C source code.
Emits direct-mapped O(1) function pointer dispatch tables and block-linking tail calls.
"""

import os
import sys
import struct
from collections import deque
from sh3_opcodes import decode_insn

class SH3Emitter:
    def __init__(self, rom_path, ram_path, out_dir="standalone/gen"):
        with open(rom_path, "rb") as f:
            self.rom = f.read()
        with open(ram_path, "rb") as f:
            self.ram = f.read()
            
        self.rom_size = len(self.rom)
        self.ram_size = len(self.ram)
        self.rom_words = struct.unpack(f">{self.rom_size//2}H", self.rom)
        self.ram_words = struct.unpack(f">{self.ram_size//2}H", self.ram)
        self.out_dir = out_dir
        os.makedirs(self.out_dir, exist_ok=True)
        
        self.blocks = {} # norm_pc -> list of DecodedInsn
        self.entry_points = set()

    def get_op(self, pc):
        norm = pc & 0x0fffffff
        if norm >= 0x0c000000:
            off = norm - 0x0c000000
            if 0 <= off + 1 < self.ram_size:
                return self.ram_words[off // 2]
        elif 0 <= norm + 1 < self.rom_size:
            return self.rom_words[norm // 2]
        return 0

    def scan_all_code(self):
        print("Discovering entry points across ROM and initial RAM...")
        # Core known vectors
        self.entry_points.add(0x00000000) # Reset Vector
        self.entry_points.add(0x0c002ac4) # Decompressor
        self.entry_points.add(0x0c002310) # Slice / Timer interrupt
        self.entry_points.add(0x0c1de164) # Post-decompress main entry
        self.entry_points.add(0x0c002a30) # Uncached P2 entry

        # Scan candidate pointers only from valid ROM code region (up to 0x51000)
        rom_code_size = min(0x51000, self.rom_size)
        rom_words32 = struct.unpack(f">{rom_code_size//4}I", self.rom[:rom_code_size])
        for w in rom_words32:
            w &= 0x0fffffff
            if (w & 1) == 0:
                if (0 <= w < rom_code_size) or (0x0c000000 <= w < 0x0c544000):
                    self.entry_points.add(w)

        # Scan candidate pointers in RAM literal pools (up to 0x544000)
        ram_code_size = min(0x544000, self.ram_size)
        ram_words32 = struct.unpack(f">{ram_code_size//4}I", self.ram[:ram_code_size])
        for w in ram_words32:
            w &= 0x0fffffff
            if (w & 1) == 0:
                if (0 <= w < rom_code_size) or (0x0c000000 <= w < 0x0c544000):
                    self.entry_points.add(w)

        print(f"Collected {len(self.entry_points)} entry points. Tracing basic blocks...")
        queue = deque(sorted(self.entry_points))

        while queue:
            pc = queue.popleft()
            norm_pc = pc & 0x0fffffff
            if norm_pc in self.blocks:
                continue

            curr = norm_pc
            block_insns = []
            is_ram = curr >= 0x0c000000
            max_addr = (0x0c000000 + ram_code_size) if is_ram else rom_code_size

            while curr < max_addr:
                op = self.get_op(curr)
                insn = decode_insn(curr, op)
                if 'unknown' in insn.mnemonic:
                    # Hit literal pool or unmapped space
                    break

                block_insns.append(insn)

                if insn.is_branch:
                    if insn.is_delay_slot:
                        delay_pc = curr + 2
                        delay_op = self.get_op(delay_pc)
                        delay_insn = decode_insn(delay_pc, delay_op)
                        block_insns.append(delay_insn)

                    if insn.branch_target is not None:
                        bt = insn.branch_target & 0x0fffffff
                        if bt not in self.blocks:
                            queue.append(bt)

                    if insn.is_call or insn.mnemonic.startswith("bt") or insn.mnemonic.startswith("bf") or insn.mnemonic.startswith("bsr") or insn.mnemonic.startswith("jsr") or insn.mnemonic.startswith("bsrf"):
                        fall = (curr + (4 if insn.is_delay_slot else 2)) & 0x0fffffff
                        if fall not in self.blocks:
                            queue.append(fall)
                    break

                curr += 2
                if len(block_insns) >= 64:
                    if curr not in self.blocks:
                        queue.append(curr)
                    break

            if block_insns:
                self.blocks[norm_pc] = block_insns

        print(f"Total compiled basic blocks: {len(self.blocks)}")

    def emit_c_code(self, num_files=16):
        print(f"Emitting C code partitioned into {num_files} files...")

        # 1. Header: sh3_types.h
        header_path = os.path.join(self.out_dir, "sh3_types.h")
        with open(header_path, "w") as f:
            f.write("""#ifndef SH3_TYPES_H
#define SH3_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sh3VM {
    uint32_t r[16];
    uint32_t pc;
    uint32_t ppc;
    uint32_t pr;
    uint32_t sr;
    uint32_t gbr;
    uint32_t vbr;
    uint32_t mach;
    uint32_t macl;
    uint32_t t;
    uint32_t spc;
    uint32_t ssr;
    int32_t  cycles;
} Sh3VM;

typedef void (*Sh3BlockFn)(Sh3VM *vm);

extern uint8_t *g_main_ram;
extern uint8_t *g_main_rom;
extern uint8_t *g_cache_ram;

extern Sh3BlockFn g_rom_blocks[2097152];
extern Sh3BlockFn g_ram_blocks[2097152];

static inline Sh3BlockFn sh3_find_block(uint32_t pc) {
    uint32_t masked = pc & 0x0fffffff;
    if (masked >= 0x0c000000) {
        uint32_t idx = (masked - 0x0c000000) >> 1;
        return (idx < 2097152) ? g_ram_blocks[idx] : 0;
    } else {
        uint32_t idx = masked >> 1;
        return (idx < 2097152) ? g_rom_blocks[idx] : 0;
    }
}

/* Fast direct memory macros */
#define READ_B(addr)      cv1k_read_b(vm, addr)
#define READ_W(addr)      cv1k_read_w(vm, addr)
#define READ_L(addr)      cv1k_read_l(vm, addr)

#define WRITE_B(addr, v)  cv1k_write_b(vm, addr, v)
#define WRITE_W(addr, v)  cv1k_write_w(vm, addr, v)
#define WRITE_L(addr, v)  cv1k_write_l(vm, addr, v)

#define READ_W_PC(addr)   cv1k_read_w_pc(vm, addr)
#define READ_L_PC(addr)   cv1k_read_l_pc(vm, addr)

uint8_t  cv1k_read_b(Sh3VM *vm, uint32_t addr);
uint16_t cv1k_read_w(Sh3VM *vm, uint32_t addr);
uint32_t cv1k_read_l(Sh3VM *vm, uint32_t addr);

void cv1k_write_b(Sh3VM *vm, uint32_t addr, uint8_t v);
void cv1k_write_w(Sh3VM *vm, uint32_t addr, uint16_t v);
void cv1k_write_l(Sh3VM *vm, uint32_t addr, uint32_t v);

uint16_t cv1k_read_w_pc(Sh3VM *vm, uint32_t addr);
uint32_t cv1k_read_l_pc(Sh3VM *vm, uint32_t addr);

void sh3_interpret_block(Sh3VM *vm);
void sh3_init_dispatch_table(void);

#ifdef __cplusplus
}
#endif

#endif /* SH3_TYPES_H */
""")

        sorted_pcs = sorted(self.blocks.keys())
        chunk_size = (len(sorted_pcs) + num_files - 1) // num_files

        for file_idx in range(num_files):
            file_pcs = sorted_pcs[file_idx * chunk_size : (file_idx + 1) * chunk_size]
            c_file_path = os.path.join(self.out_dir, f"sh3_code_{file_idx}.c")

            with open(c_file_path, "w") as f:
                f.write(f'#include "sh3_types.h"\n\n')

                # Emit block functions
                for pc in file_pcs:
                    insns = self.blocks[pc]
                    f.write(f"void block_{pc:08x}(Sh3VM *vm) {{\n")
                    f.write("    uint32_t r0 = vm->r[0], r1 = vm->r[1], r2 = vm->r[2], r3 = vm->r[3];\n")
                    f.write("    uint32_t r4 = vm->r[4], r5 = vm->r[5], r6 = vm->r[6], r7 = vm->r[7];\n")
                    f.write("    uint32_t r8 = vm->r[8], r9 = vm->r[9], r10 = vm->r[10], r11 = vm->r[11];\n")
                    f.write("    uint32_t r12 = vm->r[12], r13 = vm->r[13], r14 = vm->r[14], r15 = vm->r[15];\n")
                    f.write("    uint32_t pr = vm->pr, sr = vm->sr, gbr = vm->gbr, vbr = vm->vbr;\n")
                    f.write("    uint32_t mach = vm->mach, macl = vm->macl, t = vm->t;\n\n")

                    total_cycles = 0

                    for idx, insn in enumerate(insns):
                        total_cycles += insn.cycles
                        if insn.is_branch:
                            delay_insn = insns[idx+1] if (insn.is_delay_slot and idx + 1 < len(insns)) else None

                            if insn.mnemonic.startswith("bt/s"):
                                f.write(f"    if (t) {{\n")
                                if delay_insn:
                                    f.write(f"        /* Delay slot: {delay_insn.mnemonic} */\n")
                                    f.write(f"        {delay_insn.c_code}\n")
                                f.write(f"        vm->pc = 0x{insn.branch_target & 0xffffffff:08x};\n")
                                f.write(f"    }} else {{\n")
                                f.write(f"        vm->pc = 0x{(insn.pc + 4) & 0xffffffff:08x};\n")
                                f.write(f"    }}\n")

                            elif insn.mnemonic.startswith("bf/s"):
                                f.write(f"    if (!t) {{\n")
                                if delay_insn:
                                    f.write(f"        /* Delay slot: {delay_insn.mnemonic} */\n")
                                    f.write(f"        {delay_insn.c_code}\n")
                                f.write(f"        vm->pc = 0x{insn.branch_target & 0xffffffff:08x};\n")
                                f.write(f"    }} else {{\n")
                                f.write(f"        vm->pc = 0x{(insn.pc + 4) & 0xffffffff:08x};\n")
                                f.write(f"    }}\n")

                            elif insn.mnemonic.startswith("bt"):
                                f.write(f"    if (t) {{ vm->pc = 0x{insn.branch_target & 0xffffffff:08x}; }} else {{ vm->pc = 0x{(insn.pc + 2) & 0xffffffff:08x}; }}\n")

                            elif insn.mnemonic.startswith("bf"):
                                f.write(f"    if (!t) {{ vm->pc = 0x{insn.branch_target & 0xffffffff:08x}; }} else {{ vm->pc = 0x{(insn.pc + 2) & 0xffffffff:08x}; }}\n")

                            elif insn.mnemonic.startswith("braf"):
                                rn = (insn.op >> 8) & 0xf
                                f.write(f"    {{\n")
                                f.write(f"        uint32_t braf_pc = 0x{(insn.pc + 4) & 0xffffffff:08x} + r{rn};\n")
                                if delay_insn:
                                    f.write(f"        /* Delay slot: {delay_insn.mnemonic} */\n")
                                    f.write(f"        {delay_insn.c_code}\n")
                                f.write(f"        vm->pc = braf_pc;\n")
                                f.write(f"    }}\n")

                            elif insn.mnemonic.startswith("bsrf"):
                                rn = (insn.op >> 8) & 0xf
                                f.write(f"    {{\n")
                                f.write(f"        uint32_t bsrf_pc = 0x{(insn.pc + 4) & 0xffffffff:08x} + r{rn};\n")
                                f.write(f"        pr = 0x{(insn.pc + 4) & 0xffffffff:08x};\n")
                                if delay_insn:
                                    f.write(f"        /* Delay slot: {delay_insn.mnemonic} */\n")
                                    f.write(f"        {delay_insn.c_code}\n")
                                f.write(f"        vm->pc = bsrf_pc;\n")
                                f.write(f"    }}\n")

                            elif insn.mnemonic.startswith("bra "):
                                if delay_insn:
                                    f.write(f"    /* Delay slot: {delay_insn.mnemonic} */\n")
                                    f.write(f"    {delay_insn.c_code}\n")
                                f.write(f"    vm->pc = 0x{insn.branch_target & 0xffffffff:08x};\n")

                            elif insn.mnemonic.startswith("bsr "):
                                f.write(f"    pr = 0x{(insn.pc + 4) & 0xffffffff:08x};\n")
                                if delay_insn:
                                    f.write(f"    /* Delay slot: {delay_insn.mnemonic} */\n")
                                    f.write(f"    {delay_insn.c_code}\n")
                                f.write(f"    vm->pc = 0x{insn.branch_target & 0xffffffff:08x};\n")

                            elif insn.mnemonic == "rte":
                                f.write("    {\n")
                                f.write("        uint32_t ret_pc = vm->spc;\n")
                                f.write("        sr = vm->ssr; t = sr & 1;\n")
                                if delay_insn:
                                    f.write(f"        /* Delay slot: {delay_insn.mnemonic} */\n")
                                    f.write(f"        {delay_insn.c_code}\n")
                                f.write("        vm->pc = ret_pc;\n")
                                f.write("    }\n")

                            elif insn.mnemonic == "rts":
                                f.write("    {\n")
                                f.write("        uint32_t ret_pc = pr;\n")
                                if delay_insn:
                                    f.write(f"        /* Delay slot: {delay_insn.mnemonic} */\n")
                                    f.write(f"        {delay_insn.c_code}\n")
                                f.write("        vm->pc = ret_pc;\n")
                                f.write("    }\n")

                            elif insn.is_call: # jsr @Rn
                                rn = (insn.op >> 8) & 0xf
                                f.write(f"    {{\n")
                                f.write(f"        uint32_t call_pc = r{rn};\n")
                                f.write(f"        pr = 0x{insn.pc + 4:08x};\n")
                                if delay_insn:
                                    f.write(f"        /* Delay slot: {delay_insn.mnemonic} */\n")
                                    f.write(f"        {delay_insn.c_code}\n")
                                f.write(f"        vm->pc = call_pc;\n")
                                f.write(f"    }}\n")

                            elif insn.mnemonic.startswith("jmp"): # jmp @Rn
                                rn = (insn.op >> 8) & 0xf
                                f.write(f"    {{\n")
                                f.write(f"        uint32_t jmp_pc = r{rn};\n")
                                if delay_insn:
                                    f.write(f"        /* Delay slot: {delay_insn.mnemonic} */\n")
                                    f.write(f"        {delay_insn.c_code}\n")
                                f.write(f"        vm->pc = jmp_pc;\n")
                                f.write(f"    }}\n")
                            break
                        else:
                            f.write(f"    {insn.c_code}\n")

                    # If block didn't end with branch (fallthrough)
                    has_branch = any(i.is_branch for i in insns)
                    if not has_branch:
                        fall_pc = (insns[-1].pc + 2) & 0xffffffff
                        f.write(f"    vm->pc = 0x{fall_pc:08x};\n")

                    f.write(f"\n    vm->cycles -= {total_cycles};\n")
                    f.write("    vm->r[0]=r0; vm->r[1]=r1; vm->r[2]=r2; vm->r[3]=r3;\n")
                    f.write("    vm->r[4]=r4; vm->r[5]=r5; vm->r[6]=r6; vm->r[7]=r7;\n")
                    f.write("    vm->r[8]=r8; vm->r[9]=r9; vm->r[10]=r10; vm->r[11]=r11;\n")
                    f.write("    vm->r[12]=r12; vm->r[13]=r13; vm->r[14]=r14; vm->r[15]=r15;\n")
                    f.write("    vm->pr=pr; vm->sr=sr; vm->gbr=gbr; vm->vbr=vbr;\n")
                    f.write("    vm->mach=mach; vm->macl=macl; vm->t=t;\n")
                    f.write("}\n\n")

                # Registration function for this chunk
                f.write(f"void sh3_register_blocks_{file_idx}(void) {{\n")
                f.write("    typedef struct { uint32_t pc; Sh3BlockFn fn; } Entry;\n")
                f.write("    static const Entry s_table[] = {\n")
                for pc in file_pcs:
                    f.write(f"        {{ 0x{pc:08x}, block_{pc:08x} }},\n")
                f.write("    };\n")
                f.write("    for (size_t i = 0; i < sizeof(s_table)/sizeof(s_table[0]); ++i) {\n")
                f.write("        uint32_t pc = s_table[i].pc & 0x0fffffff;\n")
                f.write("        if (pc >= 0x0c000000) {\n")
                f.write("            uint32_t idx = (pc - 0x0c000000) >> 1;\n")
                f.write("            if (idx < 2097152) g_ram_blocks[idx] = s_table[i].fn;\n")
                f.write("        } else {\n")
                f.write("            uint32_t idx = pc >> 1;\n")
                f.write("            if (idx < 2097152) g_rom_blocks[idx] = s_table[i].fn;\n")
                f.write("        }\n")
                f.write("    }\n")
                f.write("}\n")

            print(f"Generated {c_file_path} with {len(file_pcs)} functions.")

        # Dispatch master
        tbl_path = os.path.join(self.out_dir, "sh3_dispatch_table.c")
        with open(tbl_path, "w") as f:
            f.write('#include "sh3_types.h"\n\n')
            f.write('Sh3BlockFn g_rom_blocks[2097152];\n')
            f.write('Sh3BlockFn g_ram_blocks[2097152];\n\n')
            for i in range(num_files):
                f.write(f"void sh3_register_blocks_{i}(void);\n")
            f.write("\nvoid sh3_init_dispatch_table(void) {\n")
            f.write("    memset(g_rom_blocks, 0, sizeof(g_rom_blocks));\n")
            f.write("    memset(g_ram_blocks, 0, sizeof(g_ram_blocks));\n")
            for i in range(num_files):
                f.write(f"    sh3_register_blocks_{i}();\n")
            f.write("}\n")
        print(f"Generated {tbl_path}")

if __name__ == "__main__":
    rom_path = sys.argv[1] if len(sys.argv) > 1 else "roms/ddpdfk/ddpdfk_program.bin"
    ram_path = sys.argv[2] if len(sys.argv) > 2 else "roms/ddpdfk/ddpdfk_ram_init.bin"
    out_dir  = sys.argv[3] if len(sys.argv) > 3 else "standalone/gen"
    num_files = int(sys.argv[4]) if len(sys.argv) > 4 else 16

    emitter = SH3Emitter(rom_path, ram_path, out_dir)
    emitter.scan_all_code()
    emitter.emit_c_code(num_files=num_files)
