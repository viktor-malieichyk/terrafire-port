"""TerraFire graphics-set format (DATA.PAK entries 25-51).

Layout (all little-endian), from the readers at 0x28470 and 0x26d64:

    u16 frame_count
    frame_count x { s16 w; s16 h; w*h x { u16 chunk; u8 flag } }   # tile grid
    u16 chunk_count
    chunk_count x { u8 kind; s16 bank; u16 len; u8 data[len] }

Every chunk is a 16x16 tile drawn into a 352-byte-wide off-screen buffer.
  kind 1: data is 256 opaque 8-bit pixels, row-major.
  kind 0: data is a *compiled sprite* - x86 code the game calls with EDI
          pointing at the tile's top-left byte. It consists solely of
          `mov [edi+disp], imm8/16/32` followed by `ret`; unwritten pixels
          are transparent. We decode it here rather than ever executing it.
"""
import struct

TILE = 16
STRIDE = 352  # off-screen buffer width (320 + 32 guard columns)


class Chunk:
    __slots__ = ('kind', 'bank', 'pixels', 'mask')

    def __init__(self, kind, bank, pixels, mask):
        self.kind, self.bank, self.pixels, self.mask = kind, bank, pixels, mask


def decode_compiled_sprite(code):
    """Return (pixels, mask) for a compiled 16x16 sprite."""
    pixels, mask = bytearray(TILE * TILE), bytearray(TILE * TILE)
    p = 0
    while True:
        op = code[p]
        if op == 0xc3:          # ret
            break
        width = 1
        if op == 0x66:          # operand-size prefix -> 16-bit store
            width, p = 2, p + 1
            op = code[p]
        if op == 0xc6:
            width = 1
        elif op == 0xc7:
            width = 4 if width == 1 else 2
        else:
            raise ValueError(f'unexpected opcode {op:#x} at {p}')
        modrm = code[p + 1]
        if modrm not in (0x07, 0x47, 0x87):   # [edi], [edi+disp8], [edi+disp32]
            raise ValueError(f'unexpected modrm {modrm:#x} at {p}')
        p += 2
        mod = modrm >> 6
        if mod == 0:
            disp = 0
        elif mod == 1:
            disp = struct.unpack_from('<b', code, p)[0]; p += 1
        else:
            disp = struct.unpack_from('<i', code, p)[0]; p += 4
        value = code[p:p + width]; p += width
        for k in range(width):
            row, col = divmod(disp + k, STRIDE)
            if not (0 <= row < TILE and 0 <= col < TILE):
                raise ValueError(f'store outside tile: disp={disp + k}')
            pixels[row * TILE + col] = value[k]
            mask[row * TILE + col] = 1
    if p + 1 != len(code):
        raise ValueError('trailing bytes after ret')
    return bytes(pixels), bytes(mask)


def parse(data):
    """Return (frames, chunks). frames: list of (w, h, [(chunk, flag), ...])."""
    p = 0
    (n,) = struct.unpack_from('<H', data, p); p += 2
    frames = []
    for _ in range(n):
        w, h = struct.unpack_from('<hh', data, p); p += 4
        cells = [struct.unpack_from('<HB', data, p + k * 3) for k in range(w * h)]
        p += w * h * 3
        frames.append((w, h, cells))
    (m,) = struct.unpack_from('<H', data, p); p += 2
    chunks = []
    for _ in range(m):
        kind, bank, ln = struct.unpack_from('<BhH', data, p); p += 5
        body = data[p:p + ln]; p += ln
        if kind == 0:
            pixels, mask = decode_compiled_sprite(body)
        else:
            if ln != TILE * TILE:
                raise ValueError(f'opaque chunk of length {ln}')
            pixels, mask = bytes(body), b'\x01' * (TILE * TILE)
        chunks.append(Chunk(kind, bank, pixels, mask))
    if p != len(data):
        raise ValueError(f'{len(data) - p} trailing bytes')
    return frames, chunks


def is_gfx(data):
    try:
        parse(data)
        return True
    except (ValueError, struct.error, IndexError):
        return False


def render_frame(frame, chunks, palette_rgb):
    """Render one frame to an RGBA PIL image."""
    from PIL import Image
    w, h, cells = frame
    img = Image.new('RGBA', (w * TILE, h * TILE), (0, 0, 0, 0))
    lut = [tuple(palette_rgb[i * 3:i * 3 + 3]) + (255,) for i in range(256)]
    px = img.load()
    for k, (ref, _flag) in enumerate(cells):
        c = chunks[ref]
        ox, oy = (k % w) * TILE, (k // w) * TILE
        for i in range(TILE * TILE):
            if c.mask[i]:
                px[ox + i % TILE, oy + i // TILE] = lut[c.pixels[i]]
    return img


# ---------------------------------------------------------------------------
# RLE sprite banks (DATA.PAK entries 61-106), read by 0x29640 into the
# 0x1e0-byte bank records at DAT_0008423c. The game derives 7 further
# rotated variants per sprite at load time; only the base image is stored.
#
#     u16 count
#     count x {
#         4 x { s16 x; s16 y }          # collision quad
#         s16 hot_x; s16 hot_y; s16 w; s16 h
#         w*h pixels, COLUMN-major, as alternating { u8 run; [u8 value if run] }
#         and { u8 n; u8 literal[n] } until w*h pixels are produced
#     }

class Sprite:
    __slots__ = ('quad', 'hot_x', 'hot_y', 'w', 'h', 'pixels')

    def __init__(self, quad, hot_x, hot_y, w, h, pixels):
        self.quad, self.hot_x, self.hot_y = quad, hot_x, hot_y
        self.w, self.h, self.pixels = w, h, pixels


def parse_sprites(data):
    p = 0
    (n,) = struct.unpack_from('<H', data, p); p += 2
    sprites = []
    for _ in range(n):
        quad = [struct.unpack_from('<hh', data, p + 4 * k) for k in range(4)]; p += 16
        hot_x, hot_y, w, h = struct.unpack_from('<hhhh', data, p); p += 8
        if w < 0 or h < 0:
            raise ValueError('negative sprite size')
        total, pix = w * h, bytearray()
        while len(pix) < total:
            run = data[p]; p += 1
            if run:
                pix += bytes([data[p]]) * run; p += 1
            if len(pix) == total:
                break
            lit = data[p]; p += 1
            pix += data[p:p + lit]; p += lit
        if len(pix) != total:
            raise ValueError('RLE overrun')
        sprites.append(Sprite(quad, hot_x, hot_y, w, h, bytes(pix)))
    if p != len(data):
        raise ValueError(f'{len(data) - p} trailing bytes')
    return sprites


def is_sprites(data):
    try:
        parse_sprites(data)
        return True
    except (ValueError, struct.error, IndexError):
        return False


def render_sprite(s, palette_rgb, transparent=0):
    from PIL import Image
    if not (s.w and s.h):
        return Image.new('RGBA', (1, 1), (0, 0, 0, 0))
    img = Image.new('RGBA', (s.h, s.w))   # pixels are stored column-major
    img.putdata([(0, 0, 0, 0) if c == transparent else
                 tuple(palette_rgb[c * 3:c * 3 + 3]) + (255,) for c in s.pixels])
    return img.transpose(Image.TRANSPOSE)
