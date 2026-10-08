#!/usr/bin/env python3
"""Map the fields of the saved game-state block (0x5b908, 0x2973 bytes).

Every instruction whose memory operand has an absolute displacement inside
the block is recorded with its access width, whether it is indexed (array
access), whether it is signed (movsx) and which functions touch it. Field
boundaries come from the distinct displacements; a field's size is the
largest width it is accessed with, clipped at the next known field.

Writes out/state_fields.tsv.
"""
import bisect, collections, os, sys
import capstone
from capstone import x86

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 0x10000
STATE, STATE_SIZE = 0x5b908, 0x2973


def main():
    img = open(os.path.join(ROOT, 'build/part2.bin'), 'rb').read()
    funcs = []
    for line in open(os.path.join(ROOT, 'out/functions.tsv')).read().split('\n')[1:]:
        if line:
            a, _name, size = line.split('\t')
            funcs.append((int(a, 16), int(size)))
    funcs.sort()

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    acc = collections.defaultdict(lambda: dict(widths=set(), indexed=False, signed=False,
                                               reads=0, writes=0, funcs=set()))
    for fa, size in funcs:
        for ins in md.disasm(img[fa - BASE:fa - BASE + size], fa):
            for k, op in enumerate(ins.operands):
                if op.type == x86.X86_OP_IMM and STATE <= (op.imm & 0xffffffff) < STATE + STATE_SIZE:
                    a = acc[op.imm & 0xffffffff]   # address used as a constant: array/struct base
                    a['indexed'] = True; a['funcs'].add(fa)
                    continue
                if op.type != x86.X86_OP_MEM or op.mem.segment not in (0, x86.X86_REG_DS, x86.X86_REG_SS):
                    continue
                disp = op.mem.disp & 0xffffffff
                if not STATE <= disp < STATE + STATE_SIZE:
                    continue
                if ins.mnemonic == 'lea':
                    a = acc[disp]; a['indexed'] = True; a['funcs'].add(fa)   # address taken
                    continue
                a = acc[disp]
                a['widths'].add(op.size)
                a['indexed'] |= op.mem.base != 0 or op.mem.index != 0
                a['signed'] |= ins.mnemonic.startswith('movsx')
                if k == 0 and ins.mnemonic not in ('cmp', 'test', 'push'):
                    a['writes'] += 1
                else:
                    a['reads'] += 1
                a['funcs'].add(fa)

    offs = sorted(acc)
    rows = []
    for n, d in enumerate(offs):
        a = acc[d]
        nxt = offs[n + 1] if n + 1 < len(offs) else STATE + STATE_SIZE
        width = max(a['widths']) if a['widths'] else 1
        span = nxt - d
        rows.append((d - STATE, d, min(width, span) if not a['indexed'] else span,
                     width, a['indexed'], a['signed'], a['reads'], a['writes'],
                     len(a['funcs']), ' '.join(f'{f:x}' for f in sorted(a['funcs'])[:8])))

    out = os.path.join(ROOT, 'out/state_fields.tsv')
    with open(out, 'w') as w:
        w.write('offset\taddr\tspan\taccess_width\tindexed\tsigned\treads\twrites\tnfuncs\tfuncs\n')
        for r in rows:
            w.write('%#06x\t%#x\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n' % r)

    covered = sum(min(r[2], r[3]) if not r[4] else r[2] for r in rows)
    print(f'{len(rows)} fields, {sum(1 for r in rows if r[4])} arrays/structs, '
          f'first at +{rows[0][0]:#x}, last at +{rows[-1][0]:#x}; '
          f'{covered} of {STATE_SIZE} bytes reached by direct access')


if __name__ == '__main__':
    main()
