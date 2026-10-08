#!/usr/bin/env python3
"""Disassemble the flattened image: dis.py <start hex> <end hex>"""
import sys, capstone
BASE = 0x10000
img = open(__file__.rsplit('/', 2)[0] + '/build/part2.bin', 'rb').read()
start, end = int(sys.argv[1], 16), int(sys.argv[2], 16)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
for i in md.disasm(img[start-BASE:end-BASE], start):
    print(f'{i.address:08x}  {i.mnemonic:6} {i.op_str}')
