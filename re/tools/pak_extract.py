#!/usr/bin/env python3
"""Extract TerraFire's DATA.PAK.

DATA.PAK has no header or directory. The table of contents is a u32 offset
table compiled into PART2.EXE at 0x43a60 (107 entries + a final offset that
equals the expected file size, which the game checks at startup). Element i
spans offsets[i]..offsets[i+1].

Usage: pak_extract.py <flattened part2.bin> <data.pak> <out dir>
"""
import json, os, struct, sys, wave

import tf_gfx

IMAGE_BASE = 0x10000
OFFSET_TABLE = 0x43a60
OFFSET_TABLE_END = 0x43c0c       # holds the expected DATA.PAK size
DWD_MAGIC = b'DiamondWare Digitized\n\x00\x1a'


def read_offsets(image):
    n = (OFFSET_TABLE_END - OFFSET_TABLE) // 4 + 1
    return list(struct.unpack_from(f'<{n}I', image, OFFSET_TABLE - IMAGE_BASE))


# Entries identified from their readers rather than by content.
DEMO_ENTRIES = {0, 1, 2, 3}   # 0x13f44: 0x2973-byte game-state snapshot + recorded input
COLOR_LUT_ENTRY = 60          # 0x28f10: 256x256 colour-mixing table, read to 0x73dc8
DEMO_STATE_SIZE = 0x2973


def classify(e, i=None):
    if i in DEMO_ENTRIES:
        return 'demo'
    if i == COLOR_LUT_ENTRY:
        return 'color_lut'
    if e.startswith(DWD_MAGIC):
        return 'dwd'
    if len(e) == 770 and e[:2] == b'\x00\xff' and max(e[2:]) <= 0x3f:
        return 'palette'
    if tf_gfx.is_gfx(e):
        return 'gfx'
    if tf_gfx.is_sprites(e):
        return 'sprites'
    return 'raw'


def dwd_to_wav(e, path):
    (_maj, _min, _uid, _res, comp, rate, channels, bits, _peak,
     length, _samples, data_off) = struct.unpack_from('<BBIBBHBBhIII', e, len(DWD_MAGIC))
    if comp != 0 or bits != 8:
        raise ValueError(f'unsupported DWD (compression={comp}, bits={bits})')
    pcm = bytes(b ^ 0x80 for b in e[data_off:data_off + length])  # signed -> unsigned 8-bit
    with wave.open(path, 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(1)
        w.setframerate(rate)
        w.writeframes(pcm)
    return dict(rate=rate, channels=channels, bits=bits, samples=length)


def palette_rgb(e):
    """770 bytes: 2-byte marker (00 ff) + 256 * RGB with 6-bit VGA components."""
    return [((c << 2) | (c >> 4)) for c in e[2:770]]


def palette_to_png(e, path):
    from PIL import Image
    rgb = palette_rgb(e)
    img = Image.new('RGB', (16, 16))
    img.putdata([tuple(rgb[i*3:i*3+3]) for i in range(256)])
    img.resize((256, 256), Image.NEAREST).save(path)


def gfx_to_png(e, palette, out_dir, i):
    """Render each frame of a graphics set with the given palette."""
    frames, chunks = tf_gfx.parse(e)
    for f, frame in enumerate(frames):
        tf_gfx.render_frame(frame, chunks, palette).save(os.path.join(out_dir, f'{i:03d}_{f:02d}.png'))
    return dict(frames=[[w, h] for w, h, _ in frames], chunks=len(chunks),
                compiled_sprites=sum(1 for c in chunks if c.kind == 0))


def sprites_to_png(e, palette, out_dir, i):
    """Render each sprite of an RLE bank to its own PNG and record its geometry."""
    sprites = tf_gfx.parse_sprites(e)
    meta = []
    for k, s in enumerate(sprites):
        tf_gfx.render_sprite(s, palette).save(os.path.join(out_dir, f'{i:03d}_{k:03d}.png'))
        meta.append(dict(w=s.w, h=s.h, hot=[s.hot_x, s.hot_y], quad=s.quad))
    return dict(sprites=meta)


# Palette each graphics set is drawn with, taken from the code that loads it.
# 52 and 58 are the fade-to-black/white targets; 55 is the in-game palette
# (loaded by 0x1a26c after the mission loader) and is the default.
GAMEPLAY_PALETTE = 55
GFX_PALETTE = {
    26: 54,       # 0x1a500
    27: 53, 34: 53,   # 0x12d48 (mission screen)
    43: 56,       # 0x1c4e0 (splash)
    47: 57,       # 0x1405c
}


def main(image_path, pak_path, out_dir):
    image = open(image_path, 'rb').read()
    pak = open(pak_path, 'rb').read()
    offsets = read_offsets(image)
    if offsets[-1] != len(pak):
        sys.exit(f'DATA.PAK size {len(pak)} does not match expected {offsets[-1]}')

    for sub in ('raw', 'sound', 'palette', 'gfx', 'sprites'):
        os.makedirs(os.path.join(out_dir, sub), exist_ok=True)

    palettes = {i: palette_rgb(pak[offsets[i]:offsets[i + 1]]) for i in range(52, 60)}
    index = []
    for i in range(len(offsets) - 1):
        e = pak[offsets[i]:offsets[i + 1]]
        kind = classify(e, i)
        entry = dict(index=i, offset=offsets[i], size=len(e), kind=kind)
        open(os.path.join(out_dir, 'raw', f'{i:03d}.bin'), 'wb').write(e)
        if kind == 'dwd':
            entry.update(dwd_to_wav(e, os.path.join(out_dir, 'sound', f'{i:03d}.wav')))
        elif kind == 'palette':
            palette_to_png(e, os.path.join(out_dir, 'palette', f'{i:03d}.png'))
        elif kind == 'gfx':
            pal = GFX_PALETTE.get(i, GAMEPLAY_PALETTE)
            entry.update(gfx_to_png(e, palettes[pal], os.path.join(out_dir, 'gfx'), i), palette=pal)
        elif kind == 'demo':
            entry.update(difficulty=e[0], state_size=DEMO_STATE_SIZE,
                         input_size=len(e) - DEMO_STATE_SIZE)
        elif kind == 'sprites':
            pal = GFX_PALETTE.get(i, GAMEPLAY_PALETTE)
            entry.update(sprites_to_png(e, palettes[pal], os.path.join(out_dir, 'sprites'), i), palette=pal)
        index.append(entry)

    json.dump(index, open(os.path.join(out_dir, 'index.json'), 'w'), indent=1)
    counts = {}
    for e in index:
        counts[e['kind']] = counts.get(e['kind'], 0) + 1
    print(f'extracted {len(index)} entries to {out_dir}: {counts}')


if __name__ == '__main__':
    main(*sys.argv[1:4])
