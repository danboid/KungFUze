#!/usr/bin/env python3
"""Stage 2d: the intro (the story, with the kidnapping scene, and the letter), into KFMG.DAT bank 0.

Run after kfm_game_build.py and kfm_audio_build.py. Reads build/kfm_intro.pkl (kfm_intro_capture.py)
and the intro tile indices (kfm_build.py: build/kfm_intro_tiles.pkl). Writes:
  * three screen maps (23 rows x 32 bytes, 25 used): the two story screens with their text cells
    blank, and the letter screen (the arcade's columns 4-28; the hands are cropped at the sides);
  * per story screen, its typing list: (map offset, tile) of each character in typing order, the
    arcade's text re-wrapped to 24 columns (each word keeps its own colours);
  * the story's playback: per frame, the screen, the characters typed so far, and each sprite as a
    pose (one of the game's, mirrored if need be, or one of the intro's own) and its position;
  * the intro's own poses (the ones the game does not have: Silvia, Thomas's and the grippers' other
    poses...), encoded as the game's, in bank 0.
and gen/kfm_intro.h, gen/kfm_intro.c.
"""
import collections, pickle, struct, sys
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import kfm_build as kb
import kfm_game_build as kg

COLS = 24
MAP_STRIDE = 32


# The story's sprites are drawn 1 px left and 1 px up of the arcade's places. They stand on a flat
# background, so that cannot be seen, but it lines them up with the 8x8 grid so that they need far
# fewer RAM tiles: measured over the whole intro, the cells dropped for want of RAM tiles fell from
# 1,112 to 139 (the worst frame from 25 to 8).
STORY_DX, STORY_DY = -1, -1

# The story's plain row and spare row. Screen row 1 is one flat tile on both story screens, never typed
# into and never reached by a sprite: src/main.c draws it as a single-colour row (Mode 748's mode 2),
# and the last row (22), never reached by a sprite either, uses its VRAM row (hudrow[1]) instead of
# the last playfield row's. That one, with the game's enemies and the effect voices after it, is lent
# to the RAM tiles while the story plays. The build checks both rows.
STORY_PLAIN_ROW, STORY_SPARE_ROW = 1, 22


def canon(p):
    x0 = min(e[5] for e in p)
    y0 = min(e[6] for e in p)
    return tuple(sorted((c, code, fx, fy, n, dx - x0, dy - y0) for c, code, fx, fy, n, dx, dy in p))


def mirrored(p):
    w = max(e[5] + 16 for e in p)
    return canon(tuple((c, code, fx ^ 1, fy, n, w - dx - 16, dy) for c, code, fx, fy, n, dx, dy in p))


def main():
    build, gen = ROOT / 'build', ROOT / 'gen'
    intro = pickle.loads((build / 'kfm_intro.pkl').read_bytes())
    tidx = pickle.loads((build / 'kfm_intro_tiles.pkl').read_bytes())
    gposes = pickle.loads((build / 'kfm_game_poses.pkl').read_bytes())
    d = pickle.loads((build / 'kfm_extract.pkl').read_bytes())
    sprites, pens = d['sprites'], d['pens']
    pal_pf = kb.fit332(d['hist_pf'], pens)
    map_pf = kb.pen_map(pal_pf, pens)
    dark = min(range(1, 16), key=lambda i: kb.G332_LAB[pal_pf[i]][0])
    spi = bytearray((build / 'KFMG.DAT').read_bytes())
    at = [int((build / 'kfmg_free.txt').read_text(), 0)]
    R0, NR, C0 = intro['row0'], intro['rows'], intro['col0']

    def put(data, align=1):
        a = (at[0] + align - 1) & ~(align - 1)
        if a + len(data) > 0x10000:
            raise SystemExit('the intro does not fit in SPI RAM bank 0')
        if any(spi[a:a + len(data)]):
            raise SystemExit('SPI RAM bank 0 at 0x%04X is not free' % a)
        spi[a:a + len(data)] = data
        at[0] = a + len(data)
        return a

    # ---- the screens --------------------------------------------------------------------------
    def base_map(tm, r0, blank):
        m = bytearray(NR * MAP_STRIDE)
        for r in range(NR):
            for c in range(COLS + 1):
                combo = tm[r0 + r][C0 + c] if C0 + c < 32 else blank
                m[r * MAP_STRIDE + c] = tidx.get(combo, tidx[blank])
        return m

    screens = []
    for sc in intro['screens']:
        tm = sc['static']
        typed = {(r, c) for r, c, code, at_ in sc['text']}
        bg = collections.Counter(tm[r][c] for r in range(R0, R0 + NR) for c in range(32)
                                 if (r, c) not in typed).most_common(1)[0][0]
        tm = [[bg if (r, c) in typed else tm[r][c] for c in range(32)] for r in range(32)]
        for r in range(R0, intro['static_row']):        # (the typed rows are rebuilt below)
            for c in range(32):
                tm[r][c] = bg
        m = base_map(tm, R0, bg)
        # the copyright up two rows; under it the port's credit (white, centred)
        import kfm_intro_capture as kic
        wa = kic.white_attr(intro)
        crow = max(r for r in range(NR) if any(m[r * MAP_STRIDE + c] != tidx[bg] for c in range(COLS)))
        m[(crow - 2) * MAP_STRIDE:(crow - 1) * MAP_STRIDE] = m[crow * MAP_STRIDE:(crow + 1) * MAP_STRIDE]
        for r in (crow - 1, crow):
            m[r * MAP_STRIDE:(r + 1) * MAP_STRIDE] = bytes([tidx[bg]] * (COLS + 1)) + bytes(MAP_STRIDE - COLS - 1)
        for k, ln in enumerate(kic.CREDIT):
            c0 = (COLS - len(ln)) // 2
            for i, ch in enumerate(ln):
                if ch != ' ':
                    m[(crow - 1 + k) * MAP_STRIDE + c0 + i] = tidx[(ord(ch), wa)]
        # the text, re-wrapped: words are runs of characters on one row in adjacent columns
        words, cur, last = [], [], None
        for r, c, code, at_ in sc['text']:
            if last is not None and (r != last[0] or c != last[1] + 1):
                words.append(cur)
                cur = []
            cur.append((code, at_))
            last = (r, c)
        if cur:
            words.append(cur)
        lines, line = [], []
        for w in words:
            need = len(w) + (1 if line else 0)
            if line and sum(len(x) for x in line) + len(line) - 1 + need > COLS:
                lines.append(line)
                line = []
            line.append(w)
        if line:
            lines.append(line)
        first = sc['text'][0][0] - R0 if sc['text'] else 0
        typing = []
        for k, ln in enumerate(lines):
            row = first + 2 * k
            if row >= NR:
                raise SystemExit('the story\'s text does not fit')
            col = 0
            for w in ln:
                for code, at_ in w:
                    typing.append((row * MAP_STRIDE + col, tidx[(code, at_)]))
                    col += 1
                col += 1
        screens.append((m, typing, tidx[bg]))
    # the letter: its rows centred on its content
    lt = intro['letter']
    blank = collections.Counter(c for row in lt for c in row).most_common(1)[0][0]
    rows_used = [r for r in range(32) if any(lt[r][c] != blank for c in range(32))]
    lr0 = max(0, min(32 - NR, (rows_used[0] + rows_used[-1] + 1) // 2 - NR // 2))
    letter_map = base_map(lt, lr0, blank)

    # ---- the story's sprites: the game's poses where they match, else the intro's own ---------------
    gindex = {}
    for name, (pid, pose) in gposes.items():
        gindex.setdefault(canon(pose), (pid, 0, pose))
        gindex.setdefault(mirrored(pose), (pid, 1, pose))
    # the 192-pixel window that shows the most of the scene (the arcade stages it across 256)
    xs = [e[5] for _, _, objs in intro['frames'] for o in objs for e in o]
    shift = max(range(0, 65, 4), key=lambda sh: sum(1 for x in xs if sh - 8 <= x < sh + 192 - 8))
    # The knives fly on off the right of the screen. The arcade's program clears the low knife's
    # sprite at x 224, before its screen's right edge; here a knife track that ends inside the
    # window continues at its own speed until it has left it.
    src = [(sidx, nchar, list(objs)) for sidx, nchar, objs in intro['frames']]
    def knife_at(objs, y):
        for o in objs:
            if len(o) == 1 and o[0][1] == 1 and o[0][6] == y:
                return o[0]
        return None
    extended = 0
    for i in range(1, len(src) - 1):
        for o in src[i][2]:
            if len(o) != 1 or o[0][1] != 1:
                continue
            e = o[0]
            if knife_at(src[i + 1][2], e[6]) is not None:
                continue                                  # the track goes on
            prev = knife_at(src[i - 1][2], e[6])
            if prev is None or i < 8 or knife_at(src[i - 8][2], e[6]) is None:
                continue
            v = (e[5] - knife_at(src[i - 8][2], e[6])[5]) / 8.0     # its speed (px a frame)
            if abs(v) < 0.5:
                continue
            x, k = float(e[5]), i + 1
            while k < len(src) and shift - 16 < x < shift + 192 + 8:
                x += v
                src[k][2].append(((e[0], e[1], e[2], e[3], e[4], int(round(x)), e[6]),))
                k += 1
                extended += 1
    print('knives: %d frames added, flying on off the screen' % extended)
    # The grippers: two at most (the RAM tiles hold no more). The arcade sometimes has three, and lists
    # the newest first: keeping the first two in its order swapped which ones were drawn whenever one
    # came on (the left one seemed to jump back as they walked in). Each gripper is followed from frame
    # to frame instead, and the two drawn stay drawn for as long as they are there.
    def is_grip(o):
        return any(e[0] in (4, 5) for e in o)
    keep, tracks, ntrack, shown = [], {}, 0, []
    for sidx, nchar, objs in src:
        pos = [(i, min(e[5] for e in o), min(e[6] for e in o)) for i, o in enumerate(objs) if is_grip(o)]
        # matched as a whole (the most pairs, then the least movement): when they all turn at once, each
        # one's nearest track can be its neighbour's
        def match(k, used):
            if k == len(pos):
                return (0, 0, {})
            i, x, y = pos[k]
            best = match(k + 1, used)
            for t, (tx, ty) in tracks.items():
                if t not in used and abs(x - tx) <= 14 and abs(y - ty) <= 12:
                    n, c, m = match(k + 1, used | {t})
                    cand = (n + 1, c + abs(x - tx) + abs(y - ty), {**m, i: t})
                    if (-cand[0], cand[1]) < (-best[0], best[1]):
                        best = cand
            return best
        ids = dict(match(0, frozenset())[2])
        for i, x, y in pos:                                # a new gripper: the next id
            if i not in ids:
                ids[i] = ntrack
                ntrack += 1
        tracks = {ids[i]: (x, y) for i, x, y in pos}
        seen = {t for t, (x, y) in tracks.items() if shift - 4 <= x < shift + 192}   # (mostly off the window: no slot)
        shown = [t for t in shown if t in seen]
        for t in sorted(seen):
            if len(shown) < 2 and t not in shown:
                shown.append(t)
        keep.append({i for i, _, _ in pos if ids[i] in shown})
    own = collections.OrderedDict()
    frames = []
    dropped = 0
    for fi, (sidx, nchar, objs) in enumerate(src):
        out = []
        for oi, o in enumerate(objs):
            if is_grip(o) and oi not in keep[fi]:
                continue
            ents = [(c, code, fx, fy, n, x, y) for c, code, fx, fy, n, x, y in o]
            X0 = min(e[5] for e in ents)
            Y0 = min(e[6] for e in ents)
            key = canon(ents)
            if key in gindex:
                pid, flip, pose = gindex[key]
                if flip:
                    ax = X0 + max(e[5] for e in pose)          # mirrored about anchor + 7.5
                else:
                    ax = X0 - min(e[5] for e in pose)
                ay = Y0 - min(e[6] for e in pose)
                ref = pid
            else:
                if key not in own:
                    own[key] = len(own)
                ref, flip, ax, ay = 0x80 | own[key], 0, X0, Y0
            ux, uy = ax - shift, ay - R0 * 8 + 16               # Uzebox screen x; logical y (+16: the intro's rows)
            ux, uy = ux + STORY_DX, uy + STORY_DY                 # (the grid alignment that needs fewest RAM tiles)
            if not (-32 <= ux < 224 and 0 <= uy < 256):
                dropped += 1
                continue
            out.append((ref, flip, ux + 32, uy))
        frames.append((sidx, min(255, nchar), out))
    # the intro's own poses, encoded as the game's (in bank 0)
    meta = []
    for key, k in own.items():
        canvas, X0, top = kg.compose(key, sprites, map_pf, dark)
        if canvas.shape[0] > 72:
            raise SystemExit('an intro pose is %d rows tall: more than the blitter takes' % canvas.shape[0])
        streams, spans = kg.pose_data(canvas, X0)
        a_sp = put(kg.phase_blocks(spans))
        addr = []
        for st, off in streams:
            addr.append((put(st), off, len(st)))
        meta.append((top, canvas.shape[0], addr[0], addr[1], a_sp))

    # ---- the story's plain rows --------------------------------------------------------------------
    import re
    gsrc = (gen / 'kfm_game.c').read_text()
    gmeta = gsrc[gsrc.index('kfg_meta[KFG_N_POSES]'):]
    gmeta = [(int(a), int(b)) for a, b in re.findall(r'\{\s*(-?\d+),\s*(\d+)U?,\s*0x', gmeta)]
    touched = set()
    for sidx, nchar, out in frames:
        for ref, flip, x, y in out:
            top, rows = meta[ref & 0x7F][:2] if ref & 0x80 else gmeta[ref]
            for line in range(y + top, y + top + rows):
                touched.add((line >> 3) - 2)                  # logical row - 4 + the 2 HUD rows
    plain_tile = screens[0][2]
    story_plain, why = 1, []
    for r in (STORY_PLAIN_ROW, STORY_SPARE_ROW):
        if r in touched:
            why.append('a sprite reaches row %d' % r)
    for m, typing, b in screens:
        r = STORY_PLAIN_ROW
        if b != plain_tile or any(m[r * MAP_STRIDE + c] != b for c in range(COLS + 1)):
            why.append('row %d is not one plain tile' % r)
        if any(o // MAP_STRIDE == r for o, t in typing):
            why.append('text is typed into row %d' % r)
    if why:
        print('the story keeps every row (%s)' % '; '.join(why))
        story_plain = 0
    print('story: sprites reach screen rows %d-%d; plain row %d, spare row %d: %s' %
          (min(touched), max(touched), STORY_PLAIN_ROW, STORY_SPARE_ROW, 'used' if story_plain else 'not used'))

    # ---- into bank 0 ------------------------------------------------------------------------------
    pos_maps = [put(m, 32) for m, typing, b in screens]
    pos_letter = put(letter_map, 32)
    pos_typing = [put(b''.join(struct.pack('<HB', o, t) for o, t in typing)) for m, typing, b in screens]
    stream = bytearray()
    for sidx, nchar, out in frames:
        stream += bytes([sidx, nchar, len(out)])
        for ref, flip, x, y in out:
            stream += bytes([ref, flip, x, y])
    pos_stream = put(bytes(stream))
    (build / 'KFMG.DAT').write_bytes(bytes(spi))
    (build / 'kfmg_free.txt').write_text('0x%05X\n' % at[0])

    h = ['/* Generated by tools/kfm_intro_build.py from the user\'s ROM set. Do not distribute. */',
         '#ifndef KFM_INTRO_H', '#define KFM_INTRO_H', '#include "kfm_game.h"',
         '#define KFI_ROWS %d          /* rows of each screen map (stride %d) */' % (NR, MAP_STRIDE),
         '#define KFI_N_SCREENS %d' % len(screens),
         '#define KFI_LETTER_MAP 0x%04XU' % pos_letter,
         '#define KFI_LETTER_FRAMES %dU' % intro['letter_frames'],
         '#define KFI_STREAM 0x%04XU    /* per frame: screen, characters typed, n, n x (pose, flip, x + 32, y) */' % pos_stream,
         '#define KFI_N_FRAMES %dU' % len(frames),
         '#define KFI_N_POSES %d' % max(1, len(meta)),
         '#define KFI_STORY_PLAIN %d   /* screen row %d plain, row %d spare (see kfm_intro_build.py) */' % (story_plain, STORY_PLAIN_ROW, STORY_SPARE_ROW),
         '#define KFI_PLAIN_TILE 0x%02X   /* their tile (one colour) */' % plain_tile,
         'typedef struct { unsigned int map; unsigned int typing; unsigned char n_typing; unsigned char blank; } kfi_screen_t;',
         'extern const kfi_screen_t kfi_screens[KFI_N_SCREENS];',
         'extern const kfg_meta_t kfi_meta[KFI_N_POSES];',
         '#endif']
    (gen / 'kfm_intro.h').write_text('\n'.join(h) + '\n')
    c = ['/* Generated by tools/kfm_intro_build.py. */', '#include <avr/pgmspace.h>', '#include "kfm_intro.h"',
         'const kfi_screen_t kfi_screens[KFI_N_SCREENS] PROGMEM = {%s};' % ','.join(
             '{0x%04X,0x%04X,%d,%d}' % (pm, pt, len(t), b) for pm, pt, (m, t, b) in zip(pos_maps, pos_typing, screens)),
         'const kfg_meta_t kfi_meta[KFI_N_POSES] PROGMEM = {']
    for top, rows, (a0, o0, n0), (a1, o1, n1), sp in meta or [(0, 0, (0, 0, 0), (0, 0, 0), 0)]:
        c.append('  {%d,%d,0x%04X,0x%04X,%d,%d,%d,%d,0x%04X,0},' % (top, rows, a0, a1, o0, o1, n0, n1, sp))
    c.append('};')
    (gen / 'kfm_intro.c').write_text('\n'.join(c) + '\n')
    print('story window: arcade x %d-%d' % (shift, shift + 191))
    print('intro: %d screens (%s typed), letter (rows %d-%d), %d frames, %d own poses, %d sprites off screen; '
          'stream %d bytes; bank 0 used to 0x%04X' % (len(screens), '/'.join(str(len(t)) for m, t, b in screens),
                                                      lr0, lr0 + NR - 1, len(frames), len(meta), dropped,
                                                      len(stream), at[0]))


if __name__ == '__main__':
    main()
