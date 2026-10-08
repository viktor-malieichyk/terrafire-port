#!/usr/bin/env python3
"""Run the ORIGINAL game code (flattened PART2.EXE) under the Unicorn CPU
emulator and record per-frame state while a demo plays. This is the
reference the port is compared against (see port's --trace).

Only DOS-facing routines are replaced (by address) with Python: C library
file I/O on the real game files (writes are discarded), memory allocation,
the PIT/BIOS clock, vsync, sound and interrupt-vector setup. Interrupts and
port I/O are ignored. Everything else - the game logic - runs as compiled.

Usage: trace_original.py DEMO STEPS OUT.csv [game dir]

One CSV row per object-update step (0x2cbbc), recorded on entry.
"""
import os
import struct
import sys

import time

import collections

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_INTR,
                     UC_HOOK_INSN, UC_HOOK_MEM_UNMAPPED, UcError)
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
                               UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP,
                               UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_FPCW)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE_BASE = 0x10000
LOW_SIZE = 0x200000            # low memory incl. BIOS data area, VGA and the image
HEAP_BASE, HEAP_SIZE = 0x1000000, 0x4000000
STACK_TOP = 0x85520            # end of the data object (the EXE's initial ESP)
SENTINEL = 0x1ff000            # return address that ends a call

# Globals and object fields read for the trace.
MISSION_FRAMES, QUERIES, EVENT_INDEX = 0x5b910, 0x5b81c, 0x5b874
ANGLE, SHIELDS, STAGE, TOW_ANGLE = 0x5b9af, 0x5ba9b, 0x5b9b3, 0x5b98f
RAND_A, RAND_B = 0x5b91d, 0x5b921
OBJ_TABLE, OBJ_HEAD = 0x841d4, 0x841ec
DEMO_INDEX = 0x56de0
OBJECT_UPDATE = 0x2cbbc        # trace point: one simulation step (objects_update in the port)
MISSION_TYPE = 0x5b878
TRACE_FIELDS = ('step', 'mission_type', 'mission_frames', 'queries', 'events', 'x', 'y', 'vx', 'vy',
                'angle', 'shields', 'stage', 'tow_angle', 'rand_a', 'rand_b', 'objects',
                'p_frozen', 'p_timer0', 'objhash')


class Machine:
    def __init__(self, game_dir):
        self.game_dir = game_dir
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(0, LOW_SIZE)
        self.uc.mem_map(HEAP_BASE, HEAP_SIZE)
        image = open(os.path.join(ROOT, 'build/part2.bin'), 'rb').read()
        self.uc.mem_write(IMAGE_BASE, image)
        self.uc.mem_write(SENTINEL, b'\xf4')            # hlt, never reached
        # The C startup (0x33158, skipped here) does fninit; fldcw [0x5a700]:
        # control word 0x127f = 53-bit precision, round to nearest, all
        # exceptions masked. Without it the x87 code runs at 24 bits.
        self.uc.reg_write(UC_X86_REG_FPCW, 0x127f)
        self.heap = HEAP_BASE
        self.sizes = {}
        self.files = {}                                 # FILE* -> python file
        self.clock = 0
        self.unhandled_int = set()
        self.hooks = {}
        self.trace = []
        self.frames_wanted = 0
        self.verbose = True
        self._install()

    # --------------------------------------------------------------- helpers
    def reg(self, r): return self.uc.reg_read(r)
    def setreg(self, r, v): self.uc.reg_write(r, v & 0xffffffff)
    def u32(self, a): return struct.unpack('<I', self.uc.mem_read(a, 4))[0]
    def s32(self, a): return struct.unpack('<i', self.uc.mem_read(a, 4))[0]
    def cstr(self, a):
        out = bytearray()
        while True:
            b = self.uc.mem_read(a + len(out), 1)[0]
            if b == 0: return out.decode('latin-1')
            out.append(b)

    def ret(self, value=None, pop=0):
        """Return from a hooked function (watcall: args in registers)."""
        esp = self.reg(UC_X86_REG_ESP)
        if value is not None: self.setreg(UC_X86_REG_EAX, value)
        self.setreg(UC_X86_REG_EIP, self.u32(esp))
        self.setreg(UC_X86_REG_ESP, esp + 4 + pop)

    def alloc(self, size):
        size = max(size, 1)
        p = self.heap
        self.heap += (size + 15) & ~15
        if self.heap > HEAP_BASE + HEAP_SIZE: raise MemoryError('emulated heap exhausted')
        self.uc.mem_write(p, b'\0' * size)
        self.sizes[p] = size
        return p

    # ----------------------------------------------------- replaced routines
    def _install(self):
        h = self.hooks
        h[0x26380] = lambda: self.ret(self.alloc(self.reg(UC_X86_REG_EAX)))   # game malloc
        h[0x31996] = lambda: self.ret(0)                                      # free
        h[0x3198d] = lambda: self.ret(self.sizes.get(self.reg(UC_X86_REG_EAX), 0))   # _msize
        h[0x30908] = self.fopen
        h[0x3099e] = self.fread
        h[0x30ebe] = self.fgetc
        h[0x312f6] = self.fseek
        h[0x313f2] = self.filelength
        h[0x30b68] = lambda: self.ret(0)                                      # fclose
        h[0x30cd7] = lambda: self.ret(self.reg(UC_X86_REG_EBX))               # fwrite: pretend
        h[0x31187] = lambda: self.ret(self.reg(UC_X86_REG_EAX) & 0xff)        # fputc
        h[0x2d90d] = self.read_clock                                          # PIT/BIOS clock
        h[0x2d8f0] = lambda: self.ret()                                       # vsync wait
        for a in (0x2e4a8, 0x2e6d8, 0x2e5f8, 0x2e744, 0x2e410, 0x2dda4, 0x2e3d8,
                  0x2e7a4, 0x23300):                                          # sound
            h[a] = lambda: self.ret(0)
        for a in (0x31a6e, 0x31a0f, 0x31a3a, 0x31a51):                        # int vectors, BIOS kbd
            h[a] = lambda: self.ret(0)
        h[0x318aa] = lambda: self.ret(self.alloc(self.reg(UC_X86_REG_EAX)))   # libc malloc
        h[0x30caa] = lambda: self.ret(0)                                      # time(): fixed
        for a in (0x3073f, 0x30761, 0x319f4, 0x26c2c, 0x31587):               # text output, int386
            h[a] = lambda: self.ret(0)
        h[0x31442] = self.sprintf_empty                                       # vsprintf: text only

        def on_code(uc, address, size, _):
            fn = self.hooks.get(address)
            if fn: fn()
        for a in h:
            self.uc.hook_add(UC_HOOK_CODE, on_code, begin=a, end=a)
        self.uc.hook_add(UC_HOOK_CODE, self.on_step, begin=OBJECT_UPDATE, end=OBJECT_UPDATE)

        self.recent = collections.deque(maxlen=32)
        if os.environ.get('TRACE_DEBUG'):
            self.uc.hook_add(UC_HOOK_BLOCK, lambda uc, a, sz, _: self.recent.append(a))

        def on_low(uc, address, size, _):
            esp = self.reg(UC_X86_REG_ESP)
            stack = [hex(self.u32(esp + 4 * i)) for i in range(8)]
            print(f'jumped into low memory at {address:#x}; esp={esp:#x} stack: {stack}')
            print('recent blocks:', ' '.join(hex(a) for a in self.recent))
            uc.emu_stop()
        self.uc.hook_add(UC_HOOK_CODE, on_low, begin=0, end=IMAGE_BASE - 1)

        def on_unmapped(uc, access, address, size, value, _):
            print(f'unmapped access {access} at {address:#x} (size {size}) from eip={self.reg(UC_X86_REG_EIP):#x}')
            return False
        self.uc.hook_add(UC_HOOK_MEM_UNMAPPED, on_unmapped)

        def on_int(uc, intno, _):
            self.unhandled_int.add(intno)
        self.uc.hook_add(UC_HOOK_INTR, on_int)

        def on_in(uc, port, size, _):
            return 0x08 if port == 0x3da else 0   # 0x3da: report retrace
        def on_out(uc, port, size, value, _):
            pass
        self.uc.hook_add(UC_HOOK_INSN, on_in, None, 1, 0, UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_INSN, on_out, None, 1, 0, UC_X86_INS_OUT)

    def sprintf_empty(self):
        buf = self.reg(UC_X86_REG_EAX)
        if buf: self.uc.mem_write(buf, b'\0')
        self.ret(0)

    def read_clock(self):
        self.clock += 0x400                       # ~0.86 ms of PIT time per read
        self.ret(self.clock & 0x7fffffff)

    def fopen(self):
        name = self.cstr(self.reg(UC_X86_REG_EAX))
        mode = self.cstr(self.reg(UC_X86_REG_EDX))
        path = None
        base = os.path.basename(name.replace('\\', '/'))
        for f in os.listdir(self.game_dir):
            if f.lower() == base.lower(): path = os.path.join(self.game_dir, f)
        if path is None or 'w' in mode or 'a' in mode:
            # Unknown file, or a write (settings/saves): don't touch the real files.
            if 'w' not in mode and 'a' not in mode:
                self.ret(0)
                return
            data = b''
        else:
            data = open(path, 'rb').read()
        fp = self.alloc(0x20)
        fd = 5 + len(self.files)
        self.uc.mem_write(fp + 0x10, struct.pack('<I', fd))
        self.files[fp] = {'data': data, 'pos': 0, 'fd': fd}
        self.ret(fp)

    def fread(self):
        ptr, size, n, fp = (self.reg(UC_X86_REG_EAX), self.reg(UC_X86_REG_EDX),
                            self.reg(UC_X86_REG_EBX), self.reg(UC_X86_REG_ECX))
        f = self.files[fp]
        want = size * n
        chunk = f['data'][f['pos']:f['pos'] + want]
        f['pos'] += len(chunk)
        self.uc.mem_write(ptr, chunk)
        self.ret(len(chunk) // size if size else 0)

    def fgetc(self):
        f = self.files[self.reg(UC_X86_REG_EAX)]
        if f['pos'] >= len(f['data']):
            self.ret(0xffffffff)
        else:
            self.ret(f['data'][f['pos']])
            f['pos'] += 1

    def fseek(self):
        fp, off, whence = (self.reg(UC_X86_REG_EAX), struct.unpack('<i', struct.pack('<I', self.reg(UC_X86_REG_EDX)))[0],
                           self.reg(UC_X86_REG_EBX))
        f = self.files[fp]
        f['pos'] = off if whence == 0 else f['pos'] + off if whence == 1 else len(f['data']) + off
        self.ret(0)

    def filelength(self):
        fd = self.reg(UC_X86_REG_EAX)
        for f in self.files.values():
            if f['fd'] == fd:
                self.ret(len(f['data']))
                return
        self.ret(0xffffffff)

    # ----------------------------------------------------------------- trace
    def on_step(self, uc, address, size, _):
        objs = self.u32(OBJ_TABLE)
        p14 = objs + 14 * 0x60
        live, node, objhash = 0, self.u32(OBJ_HEAD), 0
        while node and live < 1000:
            live += 1
            i = (node - objs) // 0x60
            bank = struct.unpack('<h', bytes(self.uc.mem_read(node + 0xb, 2)))[0]
            x = struct.unpack('<i', bytes(self.uc.mem_read(node + 0x12, 4)))[0] >> 8
            y = struct.unpack('<i', bytes(self.uc.mem_read(node + 0x16, 4)))[0] >> 8
            objhash = (objhash + (i * 31 + bank * 7 + x * 13 + y * 17) * (i + 1)) & 0xffffffff
            node = self.u32(node + 3)
        tow = struct.unpack('<d', self.uc.mem_read(TOW_ANGLE, 8))[0]
        timer0 = struct.unpack('<h', bytes(self.uc.mem_read(p14 + 0x4e, 2)))[0]
        self.trace.append((len(self.trace), self.s32(MISSION_TYPE), self.s32(MISSION_FRAMES),
                           self.u32(QUERIES), self.u32(EVENT_INDEX), self.s32(p14 + 0x12),
                           self.s32(p14 + 0x16), self.s32(p14 + 0x1e), self.s32(p14 + 0x22),
                           self.s32(ANGLE), self.s32(SHIELDS), self.s32(STAGE), f'{tow:.9f}',
                           self.s32(RAND_A), self.s32(RAND_B), live,
                           self.uc.mem_read(p14 + 0x44, 1)[0], timer0, objhash))
        if len(self.trace) >= self.frames_wanted: uc.emu_stop()

    # ------------------------------------------------------------------ calls
    def call(self, addr, eax=0, edx=0, ebx=0, ecx=0, stack=(), budget=2_000_000_000):
        esp = self.reg(UC_X86_REG_ESP) if self.reg(UC_X86_REG_ESP) else STACK_TOP
        for v in reversed(stack):
            esp -= 4
            self.uc.mem_write(esp, struct.pack('<I', v & 0xffffffff))
        esp -= 4
        self.uc.mem_write(esp, struct.pack('<I', SENTINEL))
        self.setreg(UC_X86_REG_ESP, esp)
        for r, v in ((UC_X86_REG_EAX, eax), (UC_X86_REG_EDX, edx), (UC_X86_REG_EBX, ebx), (UC_X86_REG_ECX, ecx)):
            self.setreg(r, v)
        t = time.time()
        try:
            self.uc.emu_start(addr, SENTINEL, count=budget)
        finally:
            if self.verbose: print(f'  call {addr:#x}: {time.time() - t:.1f}s, eip={self.reg(UC_X86_REG_EIP):#x}')
        return self.reg(UC_X86_REG_EAX)


def main(demo, frames, out, game_dir):
    m = Machine(game_dir)
    m.setreg(UC_X86_REG_ESP, STACK_TOP)
    # Startup as in 0x1006c, minus video/sound/splash/menu.
    m.call(0x27164, 0x41400, 0x3281b0, 0x28c, 0x959, stack=(0x85, 0xa28))   # engine tables
    m.call(0x12f48)                                                         # settings
    m.call(0x1c5e4)                                                         # open DATA.PAK
    fp = m.call(0x1c5b0, 0x34); m.call(0x28e3c, fp, 0x5ae78)                # fade palettes
    fp = m.call(0x1c5b0, 0x3a); m.call(0x28e3c, fp, 0x5ab70)
    fp = m.call(0x1c5b0, 0x3c); m.call(0x28f10, fp)                         # blend table
    # Play one demo (0x13f44) until enough frames are traced.
    m.uc.mem_write(DEMO_INDEX, struct.pack('<I', demo))
    # TF_DEMO_MISSION=n replays the demo's input on mission n instead (a
    # synthetic test for missions without a demo; the port honours it too).
    # TF_INVULNERABLE=1 sets the original's no-collision flag (0x5e427).
    override = os.environ.get('TF_DEMO_MISSION')
    invulnerable = os.environ.get('TF_INVULNERABLE')
    if override is not None or invulnerable:
        def patch(uc, addr, size, _):
            if override is not None:
                uc.mem_write(0x5b90c, struct.pack('<i', int(override)))   # state +0x04
            if invulnerable:
                uc.mem_write(0x5e427, b'\x01')
        m.uc.hook_add(UC_HOOK_CODE, patch, begin=0x166e8, end=0x166e8)
    m.frames_wanted = frames
    try:
        m.call(0x13f44)
    except UcError as e:
        eip = m.reg(UC_X86_REG_EIP)
        print(f'emulation stopped: {e} at eip={eip:#x} after {len(m.trace)} frames')
    with open(out, 'w') as w:
        w.write(','.join(TRACE_FIELDS) + '\n')
        for row in m.trace:
            w.write(','.join(str(v) for v in row) + '\n')
    print(f'{len(m.trace)} frames traced to {out}; interrupts seen: {sorted(m.unhandled_int)}')


if __name__ == '__main__':
    main(int(sys.argv[1]), int(sys.argv[2]), sys.argv[3],
         sys.argv[4] if len(sys.argv) > 4 else os.path.dirname(ROOT))
