#!/usr/bin/env python3
"""Flatten a Watcom/DOS4GW LE executable into a relocated memory image.

Writes <out>.bin (image starting at the lowest object base) and <out>.json
(object layout + entry point) for the Ghidra import script.
"""
import json, struct, sys
from collections import Counter

def main(src, out):
    d = open(src, 'rb').read()
    le = struct.unpack_from('<I', d, 0x3c)[0]
    assert d[le:le+2] == b'LE', 'not an LE executable'
    u32 = lambda o: struct.unpack_from('<I', d, le + o)[0]
    page_size, last_page = u32(0x28), u32(0x2c)
    n_pages = u32(0x14)
    data_pages = u32(0x80)
    fix_page_tbl, fix_rec_tbl = le + u32(0x68), le + u32(0x6c)

    objs = []
    for i in range(u32(0x44)):
        vsize, base, flags, pidx, pcnt, _ = struct.unpack_from('<6I', d, le + u32(0x40) + i*24)
        objs.append(dict(num=i+1, vsize=vsize, base=base, flags=flags, page_index=pidx, page_count=pcnt))

    lo = min(o['base'] for o in objs)
    hi = max(o['base'] + max(o['vsize'], o['page_count']*page_size) for o in objs)
    img = bytearray(hi - lo)

    def page_file_off(pnum):  # pnum is 1-based logical page
        e = d[le + u32(0x48) + (pnum-1)*4 : le + u32(0x48) + pnum*4]
        phys = (e[0] << 16) | (e[1] << 8) | e[2]
        return data_pages + (phys - 1) * page_size

    page_obj = {}
    for o in objs:
        for k in range(o['page_count']):
            pnum = o['page_index'] + k
            size = last_page if pnum == n_pages else page_size
            off = page_file_off(pnum)
            dst = o['base'] - lo + k*page_size
            img[dst:dst+size] = d[off:off+size]
            page_obj[pnum] = (o, k)

    stats = Counter()
    code_refs = set()
    code_obj = objs[0]
    obj_by_num = {o['num']: o for o in objs}
    for pnum in range(1, n_pages + 1):
        start, end = struct.unpack_from('<II', d, fix_page_tbl + (pnum-1)*4)
        p = fix_rec_tbl + start
        o, k = page_obj[pnum]
        page_va = o['base'] + k*page_size
        while p < fix_rec_tbl + end:
            stype, flags = d[p], d[p+1]; p += 2
            if stype & 0x20:
                cnt = d[p]; p += 1
            else:
                srcoffs = [struct.unpack_from('<h', d, p)[0]]; p += 2
            assert flags & 3 == 0, f'non-internal fixup flags={flags:#x}'
            if flags & 0x40:
                tobj = struct.unpack_from('<H', d, p)[0]; p += 2
            else:
                tobj = d[p]; p += 1
            kind = stype & 0x0f
            if kind == 2:   # selector only
                toff = 0
            elif flags & 0x10:
                toff = struct.unpack_from('<I', d, p)[0]; p += 4
            else:
                toff = struct.unpack_from('<H', d, p)[0]; p += 2
            if stype & 0x20:
                srcoffs = list(struct.unpack_from(f'<{cnt}h', d, p)); p += 2*cnt
            target = obj_by_num[tobj]['base'] + toff
            if tobj == code_obj['num'] and kind in (6, 7) and o['num'] != code_obj['num']:
                code_refs.add(target)  # function pointers stored in data
            for so in srcoffs:
                va = page_va + so
                i = va - lo
                if kind == 7:      # 32-bit offset
                    struct.pack_into('<I', img, i, target & 0xffffffff)
                elif kind == 8:    # 32-bit self-relative
                    struct.pack_into('<I', img, i, (target - (va + 4)) & 0xffffffff)
                elif kind == 2:    # 16-bit selector: flat model, leave as-is
                    pass
                elif kind == 5:    # 16-bit offset
                    struct.pack_into('<H', img, i, target & 0xffff)
                elif kind == 6:    # 16:32 pointer
                    struct.pack_into('<I', img, i, target & 0xffffffff)
                else:
                    raise ValueError(f'unhandled fixup type {stype:#x}')
                stats[kind] += 1

    eip = obj_by_num[u32(0x18)]['base'] + u32(0x1c)
    esp = obj_by_num[u32(0x20)]['base'] + u32(0x24)
    open(out + '.bin', 'wb').write(img)
    json.dump(dict(image_base=lo, entry=eip, esp=esp, objects=objs,
                   fixups={str(k): v for k, v in stats.items()},
                   code_refs=sorted(code_refs)),
              open(out + '.json', 'w'), indent=2)
    open(out + '.coderefs.txt', 'w').write(''.join(f'{a:#x}\n' for a in sorted(code_refs)))
    print(f'image {lo:#x}-{hi:#x} entry={eip:#x} esp={esp:#x} fixups={dict(stats)}')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
