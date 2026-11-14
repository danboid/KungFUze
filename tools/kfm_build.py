#!/usr/bin/env python3
"""Stage 2: turn build/kfm_extract.pkl into the prototype's Mode 748 assets.

Outputs (all ROM-derived; generated into gen/ and build/, never commit them):
  gen/kfm_rom.c      one blob in section .kfmrom (linked at --rom-base): recolor tables,
                     playfield and HUD palettes, 4bpp ROM tiles, mask indices
  gen/kfm_rom.h      addresses inside the blob, per-row tile windows, layout constants
  gen/kfm_tables.c   sprite poses as 8x8 blit lists, Thomas's walk sequence
  build/KFM.DAT      SPI RAM image (loaded from the SD card at boot): world map,
                     HUD map, 8x8 sprite tiles
  build/palette_preview.png  the fitted palettes applied to the floor (for checking)

Screen layout (see src/main.c): HUD rows 0-1 (lines 0-15) with their own palette, a
one-line Mode 2 separator (logical line 16) that loads the playfield palette, then the
playfield from logical line 33 = arcade line 57, 199 lines, 192 pixels = arcade x 32-223.
"""
import argparse, collections, pickle, struct
from pathlib import Path
import numpy as np

CROP_X = 32           # arcade x shown at Uzebox x 0
LOG_Y = -24           # logical scanline = arcade line + LOG_Y (arcade line 48 = tile row 6 = VRAM row 3)
MAP_ROWS = range(1, 26)   # world rows used (arcade tile rows 7..31) -> VRAM rows 4..28
WORLD_W = 224
SPI_MAP = 0x0100
SPI_HUD = SPI_MAP + len(MAP_ROWS) * WORLD_W
SPI_HUD = (SPI_HUD + 0xFF) & ~0xFF
SPI_SPR = SPI_HUD + 0x100


# ---------------------------------------------------------------------------- colour
FLASH_TOP = 0xE000                 # the program and its data end below this


def oklab(c):
    c = np.asarray(c, float) / 255
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    m1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929], [0.2119034982, 0.6806995451, 0.1073969566],
                   [0.0883024619, 0.2817188376, 0.6299787005]])
    m2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468], [1.9779984951, -2.4285922050, 0.4505937099],
                   [0.0259707400, 0.7827717716, -0.8086757660]])
    return np.cbrt(lin @ m1.T) @ m2.T


G332 = np.array([[(b & 7) * 255 // 7, ((b >> 3) & 7) * 255 // 7, ((b >> 6) & 3) * 255 // 3] for b in range(256)])
G332_LAB = oklab(G332)       # index = the Uzebox colour byte (BBGGGRRR)


def fit332(counter, pens, k=15):
    """k Uzebox colours (besides black) for a pen histogram: greedy + swap refinement in Oklab"""
    cols = collections.Counter()
    for p, n in counter.items():
        c = tuple(int(x) for x in pens[p])
        if c != (0, 0, 0):
            cols[c] += n
    rgb = np.array(list(cols), float)
    w = np.array(list(cols.values()), float)
    D = np.sqrt(((oklab(rgb)[:, None] - G332_LAB[None]) ** 2).sum(2))
    blk = D[:, 0].copy()
    cur, chosen = blk.copy(), []
    for _ in range(k):
        gain = ((cur[:, None] - np.minimum(cur[:, None], D)) * w[:, None]).sum(0)
        gain[chosen + [0]] = -1
        j = int(gain.argmax())
        chosen.append(j)
        cur = np.minimum(cur, D[:, j])
    for _ in range(8):
        better = False
        for i in range(k):
            others = [c for t, c in enumerate(chosen) if t != i]
            base = np.minimum(blk, D[:, others].min(1))
            cost = (np.minimum(base[:, None], D) * w[:, None]).sum(0)
            cost[others + [0]] = 1e18
            j = int(cost.argmin())
            if j != chosen[i] and cost[j] < (np.minimum(base, D[:, chosen[i]]) * w).sum() - 1e-9:
                chosen[i], better = j, True
        if not better:
            break
    chosen.sort(key=lambda b: G332_LAB[b][0])
    return [0] + chosen


def pen_map(palette, pens):
    lab = G332_LAB[palette]
    pl = oklab(np.asarray(pens, float))
    idx = ((pl[:, None] - lab[None]) ** 2).sum(2).argmin(1)
    idx[np.all(np.asarray(pens) == 0, axis=1)] = 0
    return idx.astype(np.uint8)


# ---------------------------------------------------------------------------- tiles
def tile_pixels(tiles, code_attr):
    cd, at = code_attr
    t = tiles[cd | ((at & 0xC0) << 2)]
    if at & 0x20:
        t = t[:, ::-1]
    return t, at & 0x1F


def pack4(px):
    """8x8 array of 4-bit indices -> 32 bytes, high nybble = left pixel"""
    px = np.asarray(px, np.uint8)
    return bytes(((px[:, 0::2] << 4) | px[:, 1::2]).ravel().tolist())


def bands_for(rows_sets, limit=256):
    """split consecutive rows into bands whose tile union fits a 256-tile window"""
    bands, cur, curset = [], [], set()
    for r, s in rows_sets:
        if len(curset | s) > limit and cur:
            bands.append((cur, curset))
            cur, curset = [], set()
        cur.append(r)
        curset |= s
    bands.append((cur, curset))
    return bands


# ---------------------------------------------------------------------------- sprites
def pose_blits(pose, sprites, quarters, rc_of, ax_off=0, ay_off=0):
    """a pose (entries relative to its anchor) -> [(quarter tile index, dx, dy, flags, recolor)]"""
    out = []
    for col, code, fx, fy, n, dx, dy in pose:
        for i in range(n):
            c = code + (n - 1 - i if fy else i)
            cell = sprites[c & 1023]
            for qy in (0, 1):
                for qx in (0, 1):
                    q = cell[qy * 8:qy * 8 + 8, qx * 8:qx * 8 + 8]
                    if not q.any():
                        continue
                    key = q.tobytes()
                    if key not in quarters:
                        quarters[key] = (len(quarters), q.copy())
                    tx = (1 - qx if fx else qx) * 8
                    ty = (1 - qy if fy else qy) * 8
                    out.append((quarters[key][0], dx + ax_off + tx, dy + ay_off + 16 * i + ty,
                                (0x01 if fx else 0) | (0x04 if fy else 0), rc_of[col]))
    return out


def strip_variants(pose_entry, sprites, map_pf, dark):
    """one arcade sprite entry (16 x 16n) -> the custom blitter's two variants.

    Each variant: n*16 row headers ((start << 4) | len, in bytes) then the rows' trimmed bytes.
    Variant 0 has pixel 0 in the high nybble of byte 0 (8 bytes a row, for even x); variant 1
    has it in the low nybble (9 bytes a row, for odd x). Colours are final palette indices;
    nybble 0 is transparent, so sprite pixels that map to black use `dark` instead."""
    col, code, fx, fy, n, dx, dy = pose_entry
    assert not fy, 'vertically flipped parts are not supported by the strip format'
    img = np.vstack([sprites[(code + i) & 1023] for i in range(n)]).astype(np.int32)
    if fx:
        img = img[:, ::-1]
    idx = np.where(img == 0, 0, map_pf[256 + col * 8 + img]).astype(np.uint8)
    idx[(img != 0) & (idx == 0)] = dark
    out = []
    for v in (0, 1):
        row_px = np.zeros((img.shape[0], 16 + 2 * v), np.uint8)
        row_px[:, v:v + 16] = idx
        if v:
            row_px = row_px[:, :18]
        by = (row_px[:, 0::2] << 4) | row_px[:, 1::2]          # 8 or 9 bytes a row
        stream = []
        for r in range(by.shape[0]):           # per row: header, then the row's trimmed bytes
            nz = np.nonzero(by[r])[0]
            if len(nz) == 0:
                stream.append(0)
                continue
            a, b = int(nz[0]), int(nz[-1])
            stream.append((a << 4) | (b - a + 1))
            stream += by[r, a:b + 1].tolist()
        out.append(bytes(stream))
    return out, len(img), dx, dy, idx != 0


def mirror(blits, axis2=16):
    """the pose facing the other way, mirrored about anchor x + axis2 / 2 (the head's / body's centre)"""
    return [(t, axis2 - (dx + 8), dy, f ^ 0x01, rc) for t, dx, dy, f, rc in blits]


def c_bytes(name, data, attrs=''):
    lines = []
    for i in range(0, len(data), 16):
        lines.append('  ' + ','.join('0x%02X' % b for b in data[i:i + 16]) + ',')
    return 'const unsigned char %s[%d] %s = {\n%s\n};\n' % (name, len(data), attrs, '\n'.join(lines))


def main():
    ap = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[1]
    ap.add_argument('--build', default=str(root / 'build'))
    ap.add_argument('--gen', default=str(root / 'gen'))
    ap.add_argument('--rom-base', default='0x8C00')
    a = ap.parse_args()
    build, gen = Path(a.build), Path(a.gen)
    gen.mkdir(exist_ok=True)
    d = pickle.loads((build / 'kfm_extract.pkl').read_bytes())
    tiles, sprites, pens = d['tiles'], d['sprites'], d['pens']
    rom_base = int(a.rom_base, 0)
    assert rom_base % 256 == 0

    # palettes ------------------------------------------------------------------
    pal_pf = fit332(d['hist_pf'], pens)
    pal_hud = fit332(d['hist_panel'], pens)
    map_pf = pen_map(pal_pf, pens)                  # 512 pens -> playfield palette index
    map_hud = pen_map(pal_hud, pens)

    def err(hist, pal, pm):
        tot = sum(hist.values())
        return sum(n * np.abs(pens[p] - G332[pal[pm[p]]]).mean() for p, n in hist.items()) / tot
    print('palette error /255 per channel: playfield %.1f, HUD %.1f' %
          (err(d['hist_pf'], pal_pf, map_pf), err(d['hist_panel'], pal_hud, map_hud)))

    # playfield tiles, by bands of rows ------------------------------------------
    cols = d['cols']
    rows_sets = [(r, {cols[c][r] for c in range(WORLD_W)}) for r in MAP_ROWS]
    bands = bands_for(rows_sets)
    blob = bytearray()

    def align(n):
        while len(blob) % n:
            blob.append(0)

    # recolor tables first (256-byte aligned): offsets 0-15 straight, then 8 per colour set
    rect = bytearray(range(16)) + bytearray(240)
    used_sets = sorted({e[0] for p in d['thomas_poses'] + d['grip_poses'] for e in p})
    rc_of = {}
    for k, s in enumerate(used_sets):
        off = 16 + 8 * k
        rc_of[s] = off
        for px in range(8):
            rect[off + px] = map_pf[256 + s * 8 + px] if px else 0
    blob += rect
    off_pal_pf = len(blob)
    blob += bytes(pal_pf)
    off_pal_hud = len(blob)
    blob += bytes(pal_hud)
    align(256)
    off_tiles = len(blob)
    tile_list = []                               # (bytes, front?) in blob order
    row_window = {}                              # world row -> (blob offset of tile 0 of the window)
    row_index = {}                               # (world row, combo) -> index in window
    for brows, bset in bands:
        align(256)
        base = len(blob)
        order = sorted(bset)
        for i, combo in enumerate(order):
            t, colour = tile_pixels(tiles, combo)
            blob += pack4(map_pf[colour * 8 + t])
            tile_list.append((colour >> 1) > 12)     # MAME's category 1: drawn over the sprites
        for r in brows:
            row_window[r] = base
            for i, combo in enumerate(order):
                row_index[(r, combo)] = i
    # HUD: two rows recomposed from the arcade panel (24 tiles each)
    panel = d['panel']
    blank = panel[1][0]
    hud_rows = [panel[0][1:22] + [blank] * 3, panel[2][0:14] + panel[3][16:26]]
    # font for the on-screen statistics: the arcade's ASCII glyphs, in the panel's red (0) and cyan (21)
    FONT = ' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/-.'
    FONT_COLS = (0, 21)
    font = [blank if ch == ' ' else (ord(ch), col) for col in FONT_COLS for ch in FONT]
    font += [(2, 20), (2, 21)]                  # energy bar segments: player (orange), enemy
    hud_set = sorted({c for r in hud_rows for c in r})
    hud_set += [c for c in dict.fromkeys(font) if c not in hud_set]
    align(256)
    off_hud_tiles = len(blob)
    for combo in hud_set:
        t, colour = tile_pixels(tiles, combo)
        blob += pack4(map_hud[colour * 8 + t])
        tile_list.append(False)
    # the intro's tiles (story and letter screens: tools/kfm_intro_capture.py), one 256-tile window, in the
    # playfield palette; before the mask and fill tables so that those cover them too (sprites are drawn there)
    off_intro_tiles = None
    intro_pkl = build / 'kfm_intro.pkl'
    if intro_pkl.exists():
        intro = pickle.loads(intro_pkl.read_bytes())
        import kfm_intro_capture as kic
        wa = kic.white_attr(intro)
        icombos = list(intro['combos']) + sorted({(ord(ch), wa) for ln in kic.CREDIT for ch in ln if ch != ' '}
                                                 - set(intro['combos']))
        if len(icombos) > 256:
            raise SystemExit('the intro uses %d tiles: more than one 256-tile window' % len(icombos))
        align(256)
        off_intro_tiles = len(blob)
        for combo in icombos:
            t, colour = tile_pixels(tiles, combo)
            blob += pack4(map_pf[colour * 8 + t])
            tile_list.append(False)
        (build / 'kfm_intro_tiles.pkl').write_bytes(pickle.dumps({c: i for i, c in enumerate(icombos)}))
    n_tiles = len(tile_list)
    off_maskidx = len(blob)
    # one mask index per 32 bytes from off_tiles (including alignment gaps: those slots are never used)
    span = (len(blob) - off_tiles) // 32
    maskidx = bytearray([0xFE] * span)
    flags_iter = iter(tile_list)
    band_starts = sorted(set(row_window.values())) + [off_hud_tiles]
    band_sizes = [len(b[1]) for b in bands] + [len(hud_set)]
    for start, size in zip(band_starts, band_sizes):
        for i in range(size):
            if next(flags_iter):
                maskidx[(start - off_tiles) // 32 + i] = 0xFF
    blob += maskidx
    # flat-colour tiles: one byte per tile slot, the fill byte, or 0x01 (unequal nybbles) if not flat
    off_fill = len(blob)
    fill = bytearray([0x01] * span)
    for k in range(span):
        t = blob[off_tiles + 32 * k:off_tiles + 32 * k + 32]
        if len(set(t)) == 1 and (t[0] >> 4) == (t[0] & 15):
            fill[k] = t[0]
    blob += fill
    n_front = sum(1 for x in maskidx if x == 0xFF)
    print('ROM tiles: %d in %d bands (+%d HUD), %d drawn over sprites; blob %d bytes at 0x%04X-0x%04X' %
          (sum(band_sizes[:-1]), len(bands), len(hud_set), n_front, len(blob), rom_base, rom_base + len(blob)))
    # Keep clear of the bootloader: Uzebox bootloaders occupy (and keep data in) the flash above
    # 0xE000 on some boards; writing there corrupted an Omega's bootloader font.
    if rom_base + len(blob) > FLASH_TOP:
        raise SystemExit('ROM blob ends at 0x%04X: past 0x%04X, the bootloader\'s area' % (rom_base + len(blob), FLASH_TOP))

    # SPI RAM image ---------------------------------------------------------------
    spi = bytearray(SPI_SPR)
    spi[0:8] = b'KFM748\x00\x01'
    for i, r in enumerate(MAP_ROWS):
        for c in range(WORLD_W):
            spi[SPI_MAP + i * WORLD_W + c] = row_index[(r, cols[c][r])]
    hud_idx = {c: i for i, c in enumerate(hud_set)}
    for i, row in enumerate(hud_rows):
        for c, combo in enumerate(row):
            spi[SPI_HUD + i * 32 + c] = hud_idx[combo]
    for i in (2, 3, 4, 5):                      # statistics and diagnostics rows start blank
        for c in range(32):
            spi[SPI_HUD + i * 32 + c] = hud_idx[blank]
    font_idx = [hud_idx[c] for c in font]
    quarters = collections.OrderedDict()
    thomas = [pose_blits(p, sprites, quarters, rc_of) for p in d['thomas_poses']]
    grips = [pose_blits(p, sprites, quarters, rc_of) for p in d['grip_poses']]
    for key, (i, q) in quarters.items():
        spi += pack4(q)
    # custom blitter strips (after the Mode 748 sprite tiles)
    nz = [i for i in range(1, 16)]
    dark = min(nz, key=lambda i: G332_LAB[pal_pf[i]][0])          # darkest non-black entry
    strips, strip_index = [], {}
    kfb_pose_strips = []
    for pose in d['thomas_poses'] + d['grip_poses']:
        lst = []
        for e in pose:
            key = e[:5]
            if key not in strip_index:
                (v0, v1), h, _, _, _ = strip_variants(e, sprites, map_pf, dark)
                a0 = len(spi)
                spi += v0
                a1 = len(spi)
                spi += v1
                strip_index[key] = len(strips)
                strips.append((a0, a1, h))
            lst.append((strip_index[key], e[5], e[6]))
        kfb_pose_strips.append(lst)
    # whole-pose streams: per base pose and anchor parity, rows of (start, len, bytes)
    pose_streams = []
    for pose in d['thomas_poses'] + d['grip_poses']:
        parts = []
        for e in pose:
            _, h, dx, dy, opaque = strip_variants(e, sprites, map_pf, dark)
            col, code, fx, fy, n, _, _ = e
            img = np.vstack([sprites[(code + i) & 1023] for i in range(n)]).astype(np.int32)
            if fx:
                img = img[:, ::-1]
            idx = np.where(img == 0, 0, map_pf[256 + col * 8 + img]).astype(np.uint8)
            idx[(img != 0) & (idx == 0)] = dark
            parts.append((dx, dy, idx))
        X0 = min(dx for dx, _, _ in parts)
        X1 = max(dx + 16 for dx, _, _ in parts)
        Y0 = min(dy for _, dy, im in parts)
        Y1 = max(dy + im.shape[0] for _, dy, im in parts)
        canvas = np.zeros((Y1 - Y0, X1 - X0), np.uint8)
        for dx, dy, im in parts:                       # later sprite entries are drawn on top
            sub = canvas[dy - Y0:dy - Y0 + im.shape[0], dx - X0:dx - X0 + 16]
            sub[im != 0] = im[im != 0]
        ys = np.nonzero(canvas.any(1))[0]
        top, bot = int(ys[0]), int(ys[-1])
        canvas = canvas[top:bot + 1]
        variants = []
        for P in (0, 1):
            shift = (P + X0) & 1                       # make (anchor x + byte 0's pixel) even
            px = np.zeros((canvas.shape[0], canvas.shape[1] + shift + 1), np.uint8)
            px[:, shift:shift + canvas.shape[1]] = canvas
            if px.shape[1] & 1:
                px = px[:, :-1]
            by = (px[:, 0::2] << 4) | px[:, 1::2]
            stream = []
            for r in range(by.shape[0]):
                nz = np.nonzero(by[r])[0]
                if len(nz) == 0:
                    stream += [0, 0]
                    continue
                a0, a1 = int(nz[0]), int(nz[-1])
                stream += [a0, a1 - a0 + 1] + by[r, a0:a1 + 1].tolist()
            addr = len(spi)
            spi += bytes(stream)
            variants.append((addr, X0 - shift, len(stream)))
        pose_streams.append((variants, Y0 + top, bot - top + 1))

    # per base pose: opaque pixel span of every row, relative to the anchor (for allocation)
    span_tab, span_info = [], []
    for pose in d['thomas_poses'] + d['grip_poses']:
        rows = collections.defaultdict(lambda: [127, -128])
        for e in pose:
            _, h, dx, dy, opaque = strip_variants(e, sprites, map_pf, dark)
            for r in range(h):
                xs = np.nonzero(opaque[r])[0]
                if len(xs):
                    sp = rows[dy + r]
                    sp[0] = min(sp[0], dx + int(xs[0]))
                    sp[1] = max(sp[1], dx + int(xs[-1]))
        top, bot = min(rows), max(rows)
        n = bot - top + 1
        phases = []
        for ph in range(8):                    # ph = (screen y of the top row) & 7
            nb = (ph + n - 1) // 8 + 1
            bands = [[127, -128] for _ in range(nb)]
            for y in range(top, bot + 1):
                if y in rows:
                    b = (ph + y - top) // 8
                    bands[b][0] = min(bands[b][0], rows[y][0])
                    bands[b][1] = max(bands[b][1], rows[y][1])
            phases.append(len(span_tab) // 2)
            span_tab.append(nb)
            span_tab.append(0)
            for b in bands:
                span_tab += b
        span_info.append((phases, top, n))
    if len(spi) > 0x10000:
        raise SystemExit('the prototype keeps everything in the low 64K of SPI RAM')
    struct.pack_into('<HHHH', spi, 8, SPI_MAP, SPI_HUD, SPI_SPR, len(quarters))
    (build / 'KFM.DAT').write_bytes(bytes(spi))
    print('SPI image %d bytes (map %d, M74 sprite tiles %d = %d bytes, custom strips %d = %d bytes)' %
          (len(spi), len(MAP_ROWS) * WORLD_W, len(quarters), 32 * len(quarters), len(strips),
           len(spi) - strips[0][0]))

    # Thomas's walk: pose sequence by frame (the arcade's: 5 frames a pose)
    seq = [p for _, p in d['thomas_seq'][13:13 + 20]]
    # gripper poses: 0-2 walking (from the left), 3-5 walking (from the right), 6 holding
    poses = []
    for b in thomas:
        poses.append(b)
    for b in thomas:
        poses.append(mirror(b))
    for b in grips:
        poses.append(b)
    for b in grips:
        poses.append(mirror(b))

    # sources ------------------------------------------------------------------------
    rom_c = ['/* Generated by tools/kfm_build.py from the user\'s ROM set. Do not distribute. */',
             '#include <avr/pgmspace.h>',
             c_bytes('kfm_rom', blob, '__attribute__((section(".kfmrom"), used))')]
    (gen / 'kfm_rom.c').write_text('\n'.join(rom_c))
    h = ['/* Generated by tools/kfm_build.py. */', '#ifndef KFM_ROM_H', '#define KFM_ROM_H',
         '#define KFM_ROM_BASE      0x%04X' % rom_base,
         '#define KFM_RECTB         0x%04X' % rom_base,
         '#define KFM_PAL_PF        0x%04X' % (rom_base + off_pal_pf),
         '#define KFM_PAL_HUD       0x%04X' % (rom_base + off_pal_hud),
         '#define KFM_TILES         0x%04X' % (rom_base + off_tiles),
         '#define KFM_HUD_TILES     0x%04X' % (rom_base + off_hud_tiles),
         '#define KFM_INTRO_TILES   0x%04X' % (rom_base + (off_intro_tiles if off_intro_tiles is not None else 0)),
         '#define KFM_MASKIDX       0x%04X' % (rom_base + off_maskidx),
         '#define KFB_FILL          0x%04X' % (rom_base + off_fill),
         '#define KFM_WORLD_W       %d' % WORLD_W,
         '#define KFM_MAP_ROWS      %d' % len(MAP_ROWS),
         '#define KFM_SPI_MAP       0x%04X' % SPI_MAP,
         '#define KFM_SPI_HUD       0x%04X' % SPI_HUD,
         '#define KFM_SPI_SPR       0x%04XUL' % SPI_SPR,
         '#define KFM_SPI_SIZE      %dUL' % len(spi),
         '#define KFM_CROP_X        %d' % CROP_X,
         '#define KFM_FONT_CHARS    "%s"' % FONT,
         '#define KFM_FONT_N        %d' % len(FONT),
         '#define KFM_LOG_Y         (%d)' % LOG_Y,
         '#define KFM_N_THOMAS      %d' % len(thomas),
         '#define KFM_POSE_THOMAS_L 0',
         '#define KFM_POSE_THOMAS_R %d' % len(thomas),
         '#define KFM_POSE_GRIP     %d' % (2 * len(thomas)),
         '#define KFM_N_GRIP_POSES  %d' % len(grips),
         '#define KFM_POSE_GRIP_M   %d' % (2 * len(thomas) + len(grips)),
         '#define KFM_THOMAS_SEQ_N  %d' % len(seq),
         '#ifndef __ASSEMBLER__',
         'extern const unsigned char kfm_row_t0[KFM_MAP_ROWS];',
         'extern const unsigned char kfm_font[2 * KFM_FONT_N + 2];  /* HUD tile per font char, red then cyan; bar tiles */',
         'extern const unsigned char kfm_pose_n[];',
         'extern const unsigned int  kfm_pose_ofs[];',
         'extern const signed char   kfm_blits[];',
         'extern const unsigned char kfm_thomas_seq[];',
         'typedef struct { unsigned int a0; unsigned int a1; unsigned char h; } kfb_strip_t;',
         'typedef struct { unsigned char strip; signed char dx; signed char dy; } kfb_ent_t;',
         'extern const kfb_strip_t kfb_strips[];',
         'extern const unsigned char kfb_pose_n[];',
         'extern const unsigned char kfb_pose_ofs[];',
         'extern const unsigned char kfb_pose_flip[];',
         'extern const kfb_ent_t kfb_ents[];',
         '/* per vertical phase: offset (in pairs) of {bands, 0} then per band {min x, max x} rel. to the anchor */',
         'typedef struct { unsigned int ofs[8]; signed char top; unsigned char rows; } kfb_span_t;',
         'extern const unsigned char kfb_pose_base[];',
         '/* whole-pose stream: SPI address, pixel offset of byte 0 (rel. anchor) and length, for anchor x even / odd */',
         'typedef struct { unsigned int a0; unsigned int a1; signed char off0; signed char off1; unsigned int n0; unsigned int n1; } kfb_pstream_t;',
         'extern const kfb_pstream_t kfb_pstreams[];',
         'extern const kfb_span_t kfb_span_info[];',
         'extern const signed char kfb_spans[];',
         '#endif',
         '#endif']
    (gen / 'kfm_rom.h').write_text('\n'.join(h) + '\n')
    t0 = [(rom_base + row_window[r]) >> 8 for r in MAP_ROWS]
    flat, ofs, ns = [], [], []
    for b in poses:
        ofs.append(len(flat) // 6)
        ns.append(len(b))
        for t, dx, dy, f, rc in b:
            flat += [t & 0xFF, t >> 8, dx, dy, f, rc]
    tab = ['/* Generated by tools/kfm_build.py. */', '#include <avr/pgmspace.h>', '#include "kfm_rom.h"',
           'const unsigned char kfm_row_t0[KFM_MAP_ROWS] PROGMEM = {%s};' % ','.join('0x%02X' % x for x in t0),
           'const unsigned char kfm_font[2 * KFM_FONT_N + 2] PROGMEM = {%s};' % ','.join(map(str, font_idx)),
           'const unsigned char kfm_pose_n[] PROGMEM = {%s};' % ','.join(map(str, ns)),
           'const unsigned int  kfm_pose_ofs[] PROGMEM = {%s};' % ','.join(map(str, ofs)),
           '/* per blit: tile lo, tile hi, dx, dy, flags, recolor offset */',
           'const signed char kfm_blits[] PROGMEM = {%s};' % ','.join(str(x if x < 128 else x - 256) for x in flat),
           'const unsigned char kfm_thomas_seq[] PROGMEM = {%s};' % ','.join(map(str, seq))]
    # custom blitter: strips {SPI address of variant 0, of variant 1, rows}; poses as strip lists.
    # Pose numbering matches the Mode 748 tables: Thomas L, Thomas R (flipped), grippers, grippers mirrored.
    tab.append('const kfb_strip_t kfb_strips[] PROGMEM = {%s};' % ','.join('{0x%04X,0x%04X,%d}' % st for st in strips))
    nT, nG = len(thomas), len(grips)
    base_of = list(range(nT)) + list(range(nT)) + [nT + i for i in range(nG)] * 2
    flip_of = [0] * nT + [1] * nT + [0] * nG + [1] * nG
    pofs, pn, pent = [], [], []
    for pi in range(len(base_of)):
        lst = kfb_pose_strips[base_of[pi]]
        pofs.append(len(pent))
        pn.append(len(lst))
        for si, dx, dy in lst:
            pent.append((si, -dx if flip_of[pi] else dx, dy))
    tab.append('const unsigned char kfb_pose_n[] PROGMEM = {%s};' % ','.join(map(str, pn)))
    tab.append('const unsigned char kfb_pose_ofs[] PROGMEM = {%s};' % ','.join(map(str, pofs)))
    tab.append('const unsigned char kfb_pose_flip[] PROGMEM = {%s};' % ','.join(map(str, flip_of)))
    tab.append('const kfb_ent_t kfb_ents[] PROGMEM = {%s};' % ','.join('{%d,%d,%d}' % e for e in pent))
    tab.append('const unsigned char kfb_pose_base[] PROGMEM = {%s};' % ','.join(map(str, base_of)))
    tab.append('const kfb_pstream_t kfb_pstreams[] PROGMEM = {%s};' % ','.join(
        '{0x%04X,0x%04X,%d,%d,%d,%d}' % (v[0][0], v[1][0], v[0][1], v[1][1], v[0][2], v[1][2])
        for v, top, n in pose_streams))
    for (v, top, n), (ph, top2, n2) in zip(pose_streams, span_info):
        assert (top, n) == (top2, n2), 'pose stream rows must match the span table'
    tab.append('const kfb_span_t kfb_span_info[] PROGMEM = {%s};' % ','.join(
        '{{%s},%d,%d}' % (','.join(map(str, ph)), top, n) for ph, top, n in span_info))
    tab.append('const signed char kfb_spans[] PROGMEM = {%s};' % ','.join(map(str, span_tab)))
    (gen / 'kfm_tables.c').write_text('\n'.join(tab) + '\n')
    print('poses: Thomas %s blits, grippers %s blits' % ([len(b) for b in thomas], [len(b) for b in grips]))
    tot = lambda lst: sum(len(spi[strips[si][0]:strips[si][1]]) - strips[si][2] for si, _, _ in lst)
    print('custom strips: Thomas %s strips / %s data bytes, grippers %s / %s' %
          ([len(l) for l in kfb_pose_strips[:nT]], [tot(l) for l in kfb_pose_strips[:nT]],
           [len(l) for l in kfb_pose_strips[nT:]], [tot(l) for l in kfb_pose_strips[nT:]]))
    print('sprite black -> palette entry %d (0x%02X)' % (dark, pal_pf[dark]))

    # preview of the converted floor (palette check) -----------------------------------
    from PIL import Image
    img = np.zeros((len(MAP_ROWS) * 8, WORLD_W * 8, 3), np.uint8)
    for i, r in enumerate(MAP_ROWS):
        for c in range(WORLD_W):
            t, colour = tile_pixels(tiles, cols[c][r])
            img[i * 8:i * 8 + 8, c * 8:c * 8 + 8] = G332[np.array(pal_pf)[map_pf[colour * 8 + t]]]
    Image.fromarray(img).save(build / 'palette_preview.png')


if __name__ == '__main__':
    main()
