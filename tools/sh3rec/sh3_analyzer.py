#!/usr/bin/env python3
"""
SH-3 Control Flow Analyzer & Basic Block Builder.
Discovers reachable code, builds Basic Blocks and Control Flow Graph.
"""

import sys
import struct
from collections import deque
from sh3_opcodes import decode_insn

class BasicBlock:
    def __init__(self, start_pc):
        self.start_pc = start_pc
        self.insns = [] # List of DecodedInsn
        self.delay_insn = None
        self.branch_insn = None
        self.successors = [] # List of target PCs
        self.fallthrough_pc = None
        self.cycles = 0

class Function:
    def __init__(self, entry_pc):
        self.entry_pc = entry_pc
        self.blocks = {} # pc -> BasicBlock

class SH3Analyzer:
    def __init__(self, rom_bytes, base_addr=0x00000000):
        self.rom = rom_bytes
        self.base = base_addr
        self.size = len(rom_bytes)
        self.words = struct.unpack(f">{self.size//2}H", self.rom)
        
        self.blocks = {} # pc -> BasicBlock
        self.functions = {} # entry_pc -> Function
        self.visited_pcs = set()
        self.queue = deque()
        self.call_targets = set()

    def get_op(self, pc):
        off = pc - self.base
        if off < 0 or off + 1 >= self.size:
            return 0
        return self.words[off // 2]

    def add_target(self, pc, is_call=False):
        # Mask off virtual memory bits (cached 0x00000000 vs uncached 0xA0000000)
        norm_pc = pc & 0x0fffffff
        if norm_pc < self.size and norm_pc not in self.visited_pcs:
            self.queue.append(norm_pc)
            if is_call:
                self.call_targets.add(norm_pc)

    def analyze(self, entry_points):
        for ep in entry_points:
            self.add_target(ep, is_call=True)

        while self.queue:
            pc = self.queue.popleft()
            if pc in self.blocks:
                continue

            block = self.trace_block(pc)
            if block:
                self.blocks[pc] = block

        print(f"Total Basic Blocks Discovered: {len(self.blocks)}")
        print(f"Total Call Targets Discovered: {len(self.call_targets)}")

    def trace_block(self, start_pc):
        curr_pc = start_pc
        block = BasicBlock(start_pc)
        
        while curr_pc < self.size:
            if curr_pc in self.visited_pcs and curr_pc != start_pc:
                # Ran into an already traced block: split / jump to it
                block.fallthrough_pc = curr_pc
                block.successors.append(curr_pc)
                return block

            self.visited_pcs.add(curr_pc)
            op = self.get_op(curr_pc)
            insn = decode_insn(curr_pc, op, self.rom)
            block.insns.append(insn)
            block.cycles += insn.cycles

            # Check if this instruction ends the block
            if insn.is_branch:
                block.branch_insn = insn
                
                # If it has a delay slot, decode the delay slot instruction
                if insn.is_delay_slot:
                    delay_pc = curr_pc + 2
                    delay_op = self.get_op(delay_pc)
                    delay_insn = decode_insn(delay_pc, delay_op, self.rom)
                    block.delay_insn = delay_insn
                    block.cycles += delay_insn.cycles
                    self.visited_pcs.add(delay_pc)
                    curr_pc = delay_pc

                # Determine successors
                if insn.branch_target is not None:
                    target = insn.branch_target & 0x0fffffff
                    block.successors.append(target)
                    self.add_target(target, is_call=insn.is_call)

                # Conditional branches also have fallthrough
                if insn.mnemonic.startswith("bt") or insn.mnemonic.startswith("bf"):
                    fall = curr_pc + 2
                    block.fallthrough_pc = fall
                    block.successors.append(fall)
                    self.add_target(fall)

                return block

            curr_pc += 2

        return block

if __name__ == "__main__":
    with open("roms/ddpdfk/ddpdfk_program.bin", "rb") as f:
        rom = f.read()
    
    analyzer = SH3Analyzer(rom, 0x00000000)
    # Entry points: Reset vector 0, and boot loader targets
    analyzer.analyze([0x00000000, 0x00002ac4, 0x00002310])
