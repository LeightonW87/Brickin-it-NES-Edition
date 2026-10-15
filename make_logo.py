#!/usr/bin/env python3
"""Turns logo_source.png into NES tiles.

  * writes the logo's tiles into tiles.chr (pattern table 0, starting at tile T_LOGO,
    which is read from config.h; up to 144 tiles are used)
  * draws the "NES EDITION" tagline in the same style (dark fill, light outline, mid-shade
    edge) from fonts/FredokaOne-Regular.ttf and writes its tiles too
  * writes logo.h: the logo's 24 x 6 tile map and the tagline's tile map

Tile slots this tool owns (it clears and rewrites them every run):
  $0E-$1F  and  T_LOGO..$FF
The logo takes tiles from T_LOGO upwards; the tagline takes $0E-$1F first and spills
into whatever is left at the top. Keep your own artwork out of those slots.

Needs Python 3 with Pillow and numpy:   pip install pillow numpy
Run it from this folder after changing logo_source.png (or the settings below),
then run make.

How the picture is simplified (the NES gives 4 colours per 16x16 area):
  background            -> colour 0 (the black backdrop)
  dark brown letter fill -> colour 1 (the palette's dark shade)
  mid brown + tan edge   -> colour 2 (the palette's medium shade)
  cream outline          -> colour 3 (the palette's light shade)
The title screen paints those with palette LOGO_PAL from config.h, so the logo
follows the colour theme.
"""
import re, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

SRC = 'logo_source.png'
# the part of the picture that is the lettering (x0, x1, y0, y1); the coloured blocks
# above it are drawn from the game's own block tiles instead
CROP = (48, 384, 64, 144)
TILES_W, TILES_H = 24, 6                  # size on screen, in tiles
# reference colours found in the picture, and which NES colour each one becomes
REF = np.array([(0, 0, 0), (100, 39, 20), (144, 80, 50), (158, 124, 96), (205, 173, 140), (233, 219, 203)], float)
ROLE = [0, 1, 2, 2, 3, 3]
BG_WEIGHT = 0.75                          # <1 favours keeping thin strokes when shrinking

def read_tile_base():
    for line in open('config.h'):
        m = re.match(r'\s*#define\s+T_LOGO\s+(\S+)', line)
        if m: return int(m.group(1), 0)
    sys.exit('T_LOGO not found in config.h')

def build():
    x0, x1, y0, y1 = CROP
    a = np.array(Image.open(SRC).convert('RGB')).astype(float)[y0:y1, x0:x1]
    cl = ((a[:, :, None, :] - REF[None, None]) ** 2).sum(axis=3).argmin(axis=2)
    role = np.array(ROLE)[cl]
    H, W = role.shape
    out = np.zeros((TILES_H * 8, TILES_W * 8), int)
    sx, sy = W / (TILES_W * 8), H / (TILES_H * 8)
    for y in range(TILES_H * 8):
        for x in range(TILES_W * 8):
            blk = role[int(y * sy):max(int((y + 1) * sy), int(y * sy) + 1),
                       int(x * sx):max(int((x + 1) * sx), int(x * sx) + 1)]
            cnt = np.bincount(blk.ravel(), minlength=4).astype(float)
            cnt[0] *= BG_WEIGHT
            out[y, x] = cnt.argmax()
    p = np.pad(out, 1, mode='edge'); new = out.copy()      # remove isolated specks
    for y in range(out.shape[0]):
        for x in range(out.shape[1]):
            n = np.bincount(p[y:y + 3, x:x + 3].ravel(), minlength=4)
            c = n.argmax()
            if n[c] >= 6 and c != out[y, x]: new[y, x] = c
    return new


# ---- the "NES EDITION" tagline ------------------------------------------------
TAG_TEXT = 'NES EDITION'
TAG_FONT = 'fonts/FredokaOne-Regular.ttf'
TAG_CAP = 11            # height of capital letters in pixels (fill only)
TAG_GAP, TAG_SPACE = 2, 4
TAG_LOW_SLOTS = list(range(0x0E, 0x20))        # free tiles between the block/frame tiles and the font
SS = 8                  # supersampling used to shrink the font to pixels

def _glyph(ch):
    size = TAG_CAP * SS
    f = ImageFont.truetype(TAG_FONT, size)
    cap_h = -f.getbbox('E', anchor='ls')[1]
    size = int(round(size * TAG_CAP * SS / cap_h)); f = ImageFont.truetype(TAG_FONT, size)
    im = Image.new('L', (int(size * 1.6), int(size * 1.5)), 0); base = int(size * 1.2)
    ImageDraw.Draw(im).text((int(size * 0.1), base), ch, font=f, fill=255, anchor='ls')
    a = np.array(im) > 127
    xs = np.where(a.any(axis=0))[0]
    a = a[base - TAG_CAP * SS:base, xs.min():xs.max() + 1]
    h, w = a.shape; wpx = -(-w // SS)
    pad = np.zeros((TAG_CAP * SS, wpx * SS), bool); pad[:, :w] = a
    return pad.reshape(TAG_CAP, SS, wpx, SS).mean(axis=(1, 3)) >= 0.5

def _shift(a, dx, dy):
    out = np.zeros_like(a); h, w = a.shape
    out[max(dy, 0):h + min(dy, 0), max(dx, 0):w + min(dx, 0)] = a[max(-dy, 0):h + min(-dy, 0), max(-dx, 0):w + min(-dx, 0)]
    return out

def build_tag():
    """16 pixels tall; colour 1 = fill, 3 = outline, 2 = edge down and to the right."""
    cols = []
    for ch in TAG_TEXT:
        cols.append(np.zeros((TAG_CAP, TAG_SPACE), bool) if ch == ' ' else _glyph(ch))
        cols.append(np.zeros((TAG_CAP, TAG_GAP), bool))
    m = np.concatenate(cols[:-1], axis=1)
    wt = -(-(m.shape[1] + 4) // 8) * 8
    canvas = np.zeros((16, wt), bool); x0 = (wt - m.shape[1]) // 2
    canvas[1:1 + TAG_CAP, x0:x0 + m.shape[1]] = m
    d = canvas.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1): d |= _shift(canvas, dx, dy)
    img = np.zeros(canvas.shape, int)
    img[(_shift(d, 1, 1) | _shift(d, 2, 2)) & ~d] = 2
    img[d & ~canvas] = 3
    img[canvas] = 1
    return img

def encode(px8):                           # 8x8 array of 0..3 -> 16 CHR bytes
    lo = []; hi = []
    for row in px8:
        a = b = 0
        for v in row:
            a = (a << 1) | (int(v) & 1); b = (b << 1) | ((int(v) >> 1) & 1)
        lo.append(a); hi.append(b)
    return bytes(lo + hi)

def pack(img):
    """Cut an image into 8x8 tiles; returns (unique tile list, map of 0 = blank / 1-based index)."""
    tiles = {}; order = []; tmap = []
    for ty in range(img.shape[0] // 8):
        for tx in range(img.shape[1] // 8):
            t = img[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
            if not t.any(): tmap.append(0); continue
            key = t.tobytes()
            if key not in tiles: tiles[key] = len(order) + 1; order.append(t)
            tmap.append(tiles[key])
    return order, tmap

def main():
    base = read_tile_base()
    logo_order, logo_map = pack(build())
    tag_img = build_tag()
    tag_order, tag_map = pack(tag_img)
    tag_w, tag_h = tag_img.shape[1] // 8, tag_img.shape[0] // 8
    logo_end = base + len(logo_order)
    if logo_end > 0x100: sys.exit('Logo needs %d tiles but only %d fit from T_LOGO' % (len(logo_order), 0x100 - base))
    tag_slots = TAG_LOW_SLOTS + list(range(logo_end, 0x100))
    if len(tag_order) > len(tag_slots):
        sys.exit('Tagline needs %d tiles but only %d are free' % (len(tag_order), len(tag_slots)))
    chr_ = bytearray(open('tiles.chr', 'rb').read())
    for n in TAG_LOW_SLOTS + list(range(base, 0x100)): chr_[n * 16:n * 16 + 16] = bytes(16)   # clear what we own
    for i, t in enumerate(logo_order): chr_[(base + i) * 16:(base + i) * 16 + 16] = encode(t)
    for i, t in enumerate(tag_order):  chr_[tag_slots[i] * 16:tag_slots[i] * 16 + 16] = encode(t)
    open('tiles.chr', 'wb').write(chr_)
    def rows(vals, w):
        return ',\n'.join('\t' + ','.join('%3d' % v for v in vals[r * w:(r + 1) * w]) for r in range(len(vals) // w))
    tag_tiles = [tag_slots[k - 1] if k else 0 for k in tag_map]       # real tile numbers
    with open('logo.h', 'w') as f:
        f.write('// Generated by make_logo.py from logo_source.png -- do not edit by hand.\n')
        f.write('// Logo: 0 = blank, otherwise logo tile number k (the real tile is T_LOGO + k - 1).\n')
        f.write('#define LOGO_W %d\n#define LOGO_H %d\n' % (TILES_W, TILES_H))
        f.write('static const unsigned char logo_map[LOGO_W * LOGO_H] = {\n' + rows(logo_map, TILES_W) + '\n};\n\n')
        f.write('// Tagline: real tile numbers (0 = blank).\n')
        f.write('#define TAG_W %d\n#define TAG_H %d\n' % (tag_w, tag_h))
        f.write('static const unsigned char tag_map[TAG_W * TAG_H] = {\n' + rows(tag_tiles, tag_w) + '\n};\n')
    print('logo: %d tiles from $%02X;  tagline "%s": %d tiles (%d x %d), slots %s;  logo.h written'
          % (len(logo_order), base, TAG_TEXT, len(tag_order), tag_w, tag_h, ','.join('$%02X' % n for n in tag_slots[:len(tag_order)][:3]) + '...'))

if __name__ == '__main__':
    main()
