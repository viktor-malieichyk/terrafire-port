#!/usr/bin/env python3
"""Reference reader/writer for TerraFire's legacy game-state formats.

This is the executable spec for the port's legacy loader (option B): parse the
original on-disk formats field by field into plain Python structures, and
serialize them back byte-for-byte. The port's C loader should mirror it.

Formats
-------
State block (0x2973 bytes, raw dump of globals at 0x5b908):
    +0x000  179 bytes of globals; see HEADER_FIELDS (offsets from out/state_fields.tsv,
            names to be filled in as the decompile is labelled)
    +0x0b3  652 x 16-byte per-object records (OBJ_SLOTS, matches the object table).
            Whole-array loops exist (bounds 651 at 0x247cc, 0x28c0 bytes at 0x20ab0);
            some slots are fixed entities accessed by absolute address (14, 15, 60,
            103, 178...). Slot 14 field 0 receives the per-difficulty value set in
            0x13f44, so slot 14 is likely the player.
    The block holds no pointers (checked against all four demos); pointer state
    lives in the separate object table, which saves translate to indices.

Demo (DATA.PAK entries 0-3, loaded by 0x13f44, cycled 0..3 by 0x13f00/0x1afd4):
    state block
    u32 events[]       strictly increasing input-query counter values
    u32 99999999       sentinel (DEMO_END)
    u32 slack[]        unused recording-buffer space after the sentinel (demos 0, 1)
    Every input query (0x18a60 / 0x18ab0) increments a global counter; during
    playback a query returns true when events[next] < counter, then advances.

Saved game GAME00N.SAV (written by 0x16bfc):
    char  stamp[25]        upper-cased date/time, NUL-terminated
    u32   mission          copy of state +0x04
    u32   difficulty       copy of state +0x00
    u32   0xcd             marker
    u8    thumb[100][160]  every 2nd pixel of every 2nd row of VGA memory
    state block
    u32   frame_counter    DAT_000841e4 (the present counter, restored by 0x2756c)
    652 x object (serialized by 0x277c4):
        u8   active        (= record byte +2)
        if active:
            u8   record[0x60]           raw, including stale pointer values
            if record.u32[0x3e] != 0:  u32 anim_index      (into table 0x56f54)
            for k in 0..2:
                if record.s16[0x4e+2k] != -1:  u32 callback_index  (into table 0x56e04,
                                                replaces pointer at record+0x54+4k)
"""
import os
import struct
import sys

STATE_SIZE = 0x2973
HEADER_SIZE = 0xb3
OBJ_SLOTS = 652
OBJ_STATE_SIZE = 16
OBJ_RECORD_SIZE = 0x60
THUMB_W, THUMB_H = 160, 100
SAV_MARKER = 0xcd

assert HEADER_SIZE + OBJ_SLOTS * OBJ_STATE_SIZE == STATE_SIZE

# (offset, struct format, name). Offsets come from tools/state_fields.py; only
# the ones confirmed so far have real names. Gaps are kept as raw bytes.
HEADER_FIELDS = [
    (0x00, '<i', 'difficulty'),   # 0..3, selects timer start values in 0x13f44
    (0x04, '<i', 'mission'),      # index into the mission table at 0x5658c
    (0x08, '<i', 'f_008'),
    (0x0c, '<i', 'f_00c'),
    (0x10, '<i', 'f_010'),
    (0x14, '<B', 'f_014'),
    (0x15, '<i', 'f_015'),
    (0x19, '<i', 'f_019'),
    (0x1d, '<B', 'f_01d'),
    (0x1e, '<B', 'f_01e'),
    (0x1f, '100s', 'message'),   # HUD message text (sprintf by 0x1d428)
    (0x83, '<i', 'message_time'),  # frames the message stays up, counted down by 0x1d104
    (0x87, '<d', 'tow_angle'),    # x87 double: angle of the tow line
    (0x8f, '<i', 'f_08f'),
    (0x93, '<i', 'f_093'),
    (0x97, '<i', 'f_097'),
    (0x9b, '<i', 'f_09b'),
    (0x9f, '<i', 'f_09f'),
    (0xa3, '<i', 'timer'),        # set from difficulty in 0x13f44 (600/425/275/125)
    (0xa7, '<i', 'f_0a7'),
    (0xab, '<i', 'stage'),        # tow stage 0/1/2
    (0xaf, '<i', 'f_0af'),
]

# Per-object 16-byte record: four 32-bit fields (field 3 is 24.8 fixed point).
OBJ_STATE_FMT = '<iiii'
OBJ_STATE_NAMES = ('o_0', 'o_4', 'o_8', 'pos')


def _check_fields():
    end = 0
    for off, fmt, name in HEADER_FIELDS:
        if off < end:
            raise AssertionError(f'field {name} overlaps previous field')
        end = off + struct.calcsize(fmt)
    if end > HEADER_SIZE:
        raise AssertionError('header fields run past the header')


_check_fields()


# --------------------------------------------------------------------------- state

def parse_state(b):
    if len(b) != STATE_SIZE:
        raise ValueError(f'state block must be {STATE_SIZE} bytes, got {len(b)}')
    header, gaps, pos = {}, {}, 0
    for off, fmt, name in HEADER_FIELDS:
        if off > pos:
            gaps[pos] = bytes(b[pos:off])
        (header[name],) = struct.unpack_from(fmt, b, off)
        pos = off + struct.calcsize(fmt)
    if pos < HEADER_SIZE:
        gaps[pos] = bytes(b[pos:HEADER_SIZE])
    objs = [dict(zip(OBJ_STATE_NAMES, struct.unpack_from(OBJ_STATE_FMT, b, HEADER_SIZE + i * OBJ_STATE_SIZE)))
            for i in range(OBJ_SLOTS)]
    return dict(header=header, gaps=gaps, objs=objs)


def write_state(s):
    b = bytearray(STATE_SIZE)
    for off, raw in s['gaps'].items():
        b[off:off + len(raw)] = raw
    for off, fmt, name in HEADER_FIELDS:
        struct.pack_into(fmt, b, off, s['header'][name])
    for i, o in enumerate(s['objs']):
        struct.pack_into(OBJ_STATE_FMT, b, HEADER_SIZE + i * OBJ_STATE_SIZE,
                         *(o[n] for n in OBJ_STATE_NAMES))
    return bytes(b)


# --------------------------------------------------------------------------- demo

DEMO_END = 99999999


def parse_demo(b):
    tail = b[STATE_SIZE:]
    if len(tail) % 4:
        raise ValueError('demo input stream is not a whole number of u32s')
    values = list(struct.unpack_from(f'<{len(tail) // 4}I', tail))
    end = values.index(DEMO_END)
    events = values[:end]
    if any(a >= b for a, b in zip(events, events[1:])):
        raise ValueError('demo events are not strictly increasing')
    return dict(state=parse_state(b[:STATE_SIZE]), events=events, slack=values[end + 1:])


def write_demo(d):
    values = d['events'] + [DEMO_END] + d['slack']
    return write_state(d['state']) + struct.pack(f'<{len(values)}I', *values)


# --------------------------------------------------------------------------- save

def save_checksum(b):
    """0x24ddc: over the file body, backwards: every non-space byte c adds
    (c ^ remaining) + random_table[c]; |sum + 0x105f7| % 900000 + 100000.
    The original appends it as a u32 (0x24d74) and checks it on reading."""
    table = struct.unpack_from('<256i', open(os.path.join(os.path.dirname(__file__), '..', 'build', 'part2.bin'), 'rb').read(), 0x43660 - 0x10000)
    s, n = 0, len(b)
    for i, c in enumerate(b):
        if c != 0x20:
            s = (s + ((c ^ (n - i)) + table[c])) & 0xffffffff
    s = (s + 0x105f7) & 0xffffffff
    if s >= 1 << 31:
        s -= 1 << 32
    return abs(s) % 900000 + 100000


def parse_save(b):
    p = 0
    stamp = b[p:p + 25].split(b'\0')[0].decode('ascii', 'replace'); p += 25
    mission, difficulty, marker = struct.unpack_from('<iiI', b, p); p += 12
    if marker != SAV_MARKER:
        raise ValueError(f'bad marker {marker:#x}')
    thumb = bytes(b[p:p + THUMB_W * THUMB_H]); p += THUMB_W * THUMB_H
    state = parse_state(b[p:p + STATE_SIZE]); p += STATE_SIZE
    (obj_header,) = struct.unpack_from('<I', b, p); p += 4
    objects = []
    for _ in range(OBJ_SLOTS):
        active = b[p]; p += 1
        if not active:
            objects.append(None)
            continue
        rec = bytes(b[p:p + OBJ_RECORD_SIZE]); p += OBJ_RECORD_SIZE
        obj = dict(record=rec, anim=None, callbacks=[None, None, None])
        if struct.unpack_from('<I', rec, 0x3e)[0] != 0:
            (obj['anim'],) = struct.unpack_from('<I', b, p); p += 4
        for k in range(3):
            if struct.unpack_from('<h', rec, 0x4e + 2 * k)[0] != -1:
                (obj['callbacks'][k],) = struct.unpack_from('<I', b, p); p += 4
        objects.append(obj)
    checksum = None
    if len(b) - p == 4:
        (checksum,) = struct.unpack_from('<i', b, p); p += 4
        if checksum != save_checksum(b[:-4]):
            raise ValueError('checksum mismatch')
    if p != len(b):
        raise ValueError(f'{len(b) - p} trailing bytes')
    return dict(stamp=stamp, mission=mission, difficulty=difficulty, thumb=thumb,
                state=state, obj_header=obj_header, objects=objects, checksum=checksum)


def write_save(s):
    out = bytearray(s['stamp'].encode('ascii')[:24].ljust(25, b'\0'))
    out += struct.pack('<iiI', s['mission'], s['difficulty'], SAV_MARKER)
    out += s['thumb']
    out += write_state(s['state'])
    out += struct.pack('<I', s['obj_header'])
    for obj in s['objects']:
        if obj is None:
            out.append(0)
            continue
        rec = obj['record']
        out.append(rec[2])
        out += rec
        if obj['anim'] is not None:
            out += struct.pack('<I', obj['anim'])
        for cb in obj['callbacks']:
            if cb is not None:
                out += struct.pack('<I', cb)
    if s.get('checksum') is not None:
        out += struct.pack('<i', save_checksum(bytes(out)))
    return bytes(out)


# --------------------------------------------------------------------------- CLI

def main(paths):
    for path in paths:
        b = open(path, 'rb').read()
        if path.lower().endswith('.sav'):
            s = parse_save(b)
            ok = write_save(s) == b
            print(f'{path}: save, mission {s["mission"]}, difficulty {s["difficulty"]}, '
                  f'{sum(o is not None for o in s["objects"])} live objects, round-trip {"OK" if ok else "FAIL"}')
        else:
            d = parse_demo(b)
            ok = write_demo(d) == b
            h = d['state']['header']
            print(f'{path}: demo, mission {h["mission"]}, difficulty {h["difficulty"]}, '
                  f'{len(d["events"])} events, round-trip {"OK" if ok else "FAIL"}')
        if not ok:
            sys.exit(1)


if __name__ == '__main__':
    main(sys.argv[1:])
