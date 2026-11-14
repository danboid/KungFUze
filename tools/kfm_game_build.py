#!/usr/bin/env python3
"""Stage 2b: the playable floor's sprite poses and Thomas's move tables.

Run after kfm_build.py. Appends the game's poses to build/KFM.DAT in SPI RAM bank 1
(0x10000 up), each as two pre-shifted streams (even / odd anchor x) plus span tables
for the eight vertical phases, and writes gen/kfm_game.h and gen/kfm_game.c.
"""
import argparse, collections, pickle, struct
from pathlib import Path
import numpy as np
import kfm_build as kb

ROOT = Path(__file__).resolve().parents[1]
BANK1 = 0x10000
BANK0_KEEP = 0x1800             # KFMG.DAT keeps bank 0's floor map (0x0100) and HUD map (0x1700) only
SPAN_BLOCK = 22                 # 1 byte band count, 10 x (min, max), 1 spare

# enemy poses used, by capture index (see the capture's numbered sheets)
GRIP = {'walk0': 0, 'walk1': 1, 'walk2': 2, 'reach0': 3, 'reach1': 4, 'reach2': 5,
        'hit': 6, 'fall0': 7, 'fall1': 8, 'hold': 9}
KNIFE = {'walk0': 0, 'walk1': 1, 'walk2': 2, 'throw_hi': 3, 'throw_lo0': 15, 'throw_lo1': 14, 'aim': 7}
BOSS = {'walk0': 0, 'walk1': 1, 'walk2': 2, 'down': 3, 'crouch0': 4, 'thrust_lo': 5, 'raise': 7,
        'thrust_hi': 8, 'swing': 9, 'crouch1': 3}   # (the boss's sprites are captured together)
# sanity checks on the chosen poses (rows, width): the capture's order depends on how its
# session plays out, so a change to the tools could reorder it; fail loudly rather than mislabel
EXPECT = {'grip_walk': (54, 62, 16, 30), 'grip_reach': (60, 66, 14, 34), 'grip_fall': (44, 56, 34, 44),
          'grip_hold': (54, 60, 26, 36), 'knife_walk': (55, 62, 14, 26), 'knife_throw_hi': (55, 62, 28, 36),
          'boss_walk': (60, 66, 28, 44), 'boss_down': (44, 64, 30, 56), 'boss_thrust_hi': (55, 70, 44, 70),
          'boss_thrust_lo': (36, 60, 44, 70), 'knife_aim': (55, 62, 26, 36), 'pop_': (4, 16, 8, 16),
          'knife_raise': (64, 80, 12, 30), 'blade': (2, 8, 8, 16)}
MOVE_NAMES = {}   # (captures are named by their real moves)


def compose(pose, sprites, map_pf, dark):
    """sprite entries -> palette-index canvas, its origin (x0, y0) relative to the anchor"""
    x0 = min(e[5] for e in pose)
    y0 = min(e[6] for e in pose)
    w = max(e[5] + 16 for e in pose) - x0
    h = max(e[6] + 16 * e[4] for e in pose) - y0
    canvas = np.zeros((h, w), np.uint8)
    for col, code, fx, fy, n, dx, dy in pose:          # later entries on top
        img = np.vstack([sprites[(code + (n - 1 - i if fy else i)) & 1023] for i in range(n)]).astype(np.int32)
        if fx:
            img = img[:, ::-1]
        if fy:
            img = img[::-1]
        idx = np.where(img == 0, 0, map_pf[256 + col * 8 + img]).astype(np.uint8)
        idx[(img != 0) & (idx == 0)] = dark
        sub = canvas[dy - y0:dy - y0 + img.shape[0], dx - x0:dx - x0 + 16]
        sub[idx != 0] = idx[idx != 0]
    ys = np.nonzero(canvas.any(1))[0]
    canvas = canvas[ys[0]:ys[-1] + 1]
    return canvas, x0, y0 + int(ys[0])


def pose_data(canvas, X0):
    """two pre-shifted streams and the per-row opaque spans"""
    streams = []
    for P in (0, 1):
        shift = (P + X0) & 1
        px = np.zeros((canvas.shape[0], canvas.shape[1] + shift + 1), np.uint8)
        px[:, shift:shift + canvas.shape[1]] = canvas
        if px.shape[1] & 1:
            px = px[:, :-1]
        by = (px[:, 0::2] << 4) | px[:, 1::2]
        st = []
        for r in range(by.shape[0]):
            nz = np.nonzero(by[r])[0]
            if len(nz) == 0:
                st += [0, 0]
                continue
            a0, a1 = int(nz[0]), int(nz[-1])
            st += [a0, a1 - a0 + 1] + by[r, a0:a1 + 1].tolist()
        streams.append((bytes(st), X0 - shift))
    spans = []
    for r in range(canvas.shape[0]):
        xs = np.nonzero(canvas[r])[0]
        spans.append((X0 + int(xs[0]), X0 + int(xs[-1])) if len(xs) else None)
    return streams, spans


def phase_blocks(spans):
    out = bytearray()
    n = len(spans)
    for ph in range(8):
        nb = (ph + n - 1) // 8 + 1
        assert nb <= 10, 'pose taller than 72 rows'
        bands = [[127, -128] for _ in range(nb)]
        for r, sp in enumerate(spans):
            if sp:
                b = (ph + r) // 8
                bands[b][0] = min(bands[b][0], sp[0])
                bands[b][1] = max(bands[b][1], sp[1])
        blk = bytearray([nb])
        for lo, hi in bands:
            blk += struct.pack('bb', lo, hi)
        out += blk + bytes(SPAN_BLOCK - len(blk))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default=str(ROOT / 'build'))
    ap.add_argument('--gen', default=str(ROOT / 'gen'))
    a = ap.parse_args()
    build, gen = Path(a.build), Path(a.gen)
    d = pickle.loads((build / 'kfm_extract.pkl').read_bytes())
    cap = pickle.loads((build / 'kfm_capture.pkl').read_bytes())
    sprites, pens = d['sprites'], d['pens']
    pal_pf = kb.fit332(d['hist_pf'], pens)            # the same palette as kfm_build.py
    map_pf = kb.pen_map(pal_pf, pens)
    dark = min(range(1, 16), key=lambda i: kb.G332_LAB[pal_pf[i]][0])

    poses = collections.OrderedDict()                 # name -> entries (dx from anchor, dy from feet)
    facing = {}

    # Thomas: walk (from the extraction, re-anchored on the feet), then the captured moves
    walk = []
    for p in d['thomas_poses']:
        b = max(e[6] + 16 * e[4] for e in p)
        walk.append(tuple((c, code, fx, fy, n, dx, dy - b) for c, code, fx, fy, n, dx, dy in p))
    walk_seq = [walk.index(walk[i]) for i in [s for _, s in d['thomas_seq'][13:33]]]
    shapes = []
    for i, p in enumerate(walk):
        poses['thomas_walk%d' % i] = p
        shapes.append(p)
    moves = {}
    for cname, frames in cap['moves'].items():
        name = MOVE_NAMES.get(cname, cname)
        while len(frames) > 1 and frames[-1] == frames[-2]:
            frames = frames[:-1]
        seq = []
        for shape, yoff in frames:
            if shape not in shapes:
                shapes.append(shape)
                poses['thomas_%d' % (len(shapes) - 1)] = shape
            seq.append((shapes.index(shape), yoff))
        moves[name] = seq
    stand_feet = None
    for k, sid in [('grip', GRIP), ('knife', KNIFE), ('boss', BOSS)]:
        lst = cap['enemies'][k]
        for nm, i in sid.items():
            poses['%s_%s' % (k, nm)] = lst[i][0]
            seen, left_of = lst[i][1]
            if seen < 10:     # a pose seen only a few times is usually two enemies merged into one
                raise SystemExit('pose %s_%s (capture index %d) was seen only %d times: two enemies merged? '
                                 'check the numbered sheets' % (k, nm, i, seen))
            facing['%s_%s' % (k, nm)] = 0 if left_of * 2 >= seen else 1   # 1: faces left natively
    # the thrower raising his knife overhead (thrower + held knife, colours 6 and 7)
    kh = cap['enemies'].get('knife_hold', [])
    if not kh:
        raise SystemExit('the knife thrower\'s raised-knife pose was not captured')
    best = max(range(len(kh)), key=lambda i: kh[i][1][0])
    poses['knife_raise'] = kh[best][0]
    facing['knife_raise'] = 0 if kh[best][1][1] * 2 >= kh[best][1][0] else 1
    # the knife thrower's death (kfm_capture.py: capture_knife_death): its shapes, facing right as
    # captured (he falls to the left, away from Thomas); the game mirrors them for the other side
    kd = cap.get('knife_die')
    if not kd:
        raise SystemExit('the knife thrower\'s death was not captured: run kfm_capture.py again')
    for i, sh in enumerate(kd['shapes']):
        poses['knife_die%d' % i] = sh
        facing['knife_die%d' % i] = 0
    # the flying knife: code 1 in colour 1 (red handle, blade to the right); anchored top-left
    poses['blade'] = ((1, 1, 0, 0, 1, 0, 0),)
    poses['strike'] = ((11, 268, 0, 0, 1, 0, 0),)         # the red strike where a knife hits Thomas
    # score pop-ups: one sprite each, colour 12 (codes from the sprite ROM's pop-up strip)
    for code, nm in ((760, 'pop_100'), (752, 'pop_200'), (755, 'pop_500'), (757, 'pop_800')):
        poses[nm] = ((12, code, 0, 0, 1, 0, 0),)

    # build the streams
    import re
    bank0 = int(re.search(r'KFM_SPI_SIZE\s+(\d+)UL', (gen / 'kfm_rom.h').read_text()).group(1))
    bench = bytearray((build / 'KFM.DAT').read_bytes())[:bank0]  # (re-runnable: drop an earlier bank 1 part)
    (build / 'KFM.DAT').write_bytes(bytes(bench))               # the benchmark's file: bank 0 only
    # the game's own file, KFMG.DAT: of bank 0 only the floor map and the HUD map (below KFG_BANK0_FREE);
    # the rest of bank 0 is for the sound (kfm_audio_build.py) and the intro (kfm_intro_build.py)
    spi = bench[:BANK0_KEEP] + bytes(BANK1 - BANK0_KEEP)
    meta, ids, native_left = [], {}, []
    b0 = [BANK0_KEEP]                             # the next free byte of bank 0
    reach = {}
    for name, pose in poses.items():
        canvas, X0, top = compose(pose, sprites, map_pf, dark)
        for k, (r0, r1, w0, w1) in EXPECT.items():
            if name.startswith(k) and not (r0 <= canvas.shape[0] <= r1 and w0 <= canvas.shape[1] <= w1):
                raise SystemExit('pose %s is %d x %d, not what was expected: the capture order changed; '
                                 'check the numbered sheets' % (name, canvas.shape[0], canvas.shape[1]))
        streams, spans = pose_data(canvas, X0)
        if name.startswith('knife_die'):
            # bank 0, after its floor and HUD maps (bank 1 is full; the sound and the intro follow)
            def put0(b):
                a = b0[0]
                spi[a:a + len(b)] = b
                b0[0] += len(b)
                return a
            a_sp = put0(phase_blocks(spans))
            addr = [(put0(st), off, len(st)) for st, off in streams]
            ids[name] = len(meta)
            meta.append((top, canvas.shape[0], addr[0], addr[1], a_sp, 0))
        else:
            a_sp = len(spi) - BANK1
            spi += phase_blocks(spans)
            addr = []
            for st, off in streams:
                addr.append((len(spi) - BANK1, off, len(st)))
                spi += st
            ids[name] = len(meta)
            meta.append((top, canvas.shape[0], addr[0], addr[1], a_sp, 1))
        xs = [sp[0] for sp in spans if sp]
        reach[name] = -min(xs) if xs else 0                   # how far it extends to the left
        # native facing (enemies): the arcade flips left-facing sprites; 1 = this pose faces left
        native_left.append(facing.get(name, 0))
    if len(spi) > 0x1F000:
        raise SystemExit('SPI image too large (%d bytes)' % len(spi))
    # a build ID, in the file (bytes 4-7) and in the firmware (KFG_BUILD_ID): the game refuses a
    # KFMG.DAT from another asset build (its sound, poses and intro are at other addresses)
    import os
    build_id = int.from_bytes(os.urandom(4), 'little')
    spi[4:8] = build_id.to_bytes(4, 'little')
    (build / 'KFMG.DAT').write_bytes(bytes(spi))
    (build / 'kfmg_free.txt').write_text('0x%05X\n' % ((b0[0] + 15) & ~15))   # the next free byte of bank 0
    (build / 'kfm_game_poses.pkl').write_bytes(pickle.dumps(
        {name: (ids[name], pose) for name, pose in poses.items()}))  # for the intro (kfm_intro_build.py)
    print('game poses: %d, bank 1 data %d bytes, KFMG.DAT %d bytes' % (len(meta), len(spi) - BANK1, len(spi)))

    # Thomas's moves: shape ids, feet offsets, the frame that reaches furthest
    sid_of = {i: ids['thomas_walk%d' % i] if i < len(walk) else ids['thomas_%d' % i] for i in range(len(shapes))}
    mv_names = ['kick', 'punch', 'crouch', 'crouch_kick', 'crouch_punch', 'jump', 'jump_kick', 'death']
    flat_s, flat_y, mv = [], [], []
    press = cap.get('press', {})
    for nm in mv_names:
        seq = moves[nm]
        pr = press.get(nm, 0)
        if nm.startswith('crouch_'):
            seq = seq[pr:]                    # the game starts these already crouching
            pr = 0
            base = moves['crouch'][min(4, len(moves['crouch']) - 1)][0]
            k0 = max(range(len(seq)), key=lambda k: reach[list(poses)[sid_of[seq[k][0]]]])
            for k in range(k0 + 1, len(seq)):  # ...and end them back in the plain crouch
                if seq[k][0] == base:
                    seq = seq[:k + 1]
                    break
        start = len(flat_s)
        best, bi = -999, pr
        for k, (s_, yo) in enumerate(seq):
            r = reach[list(poses)[sid_of[s_]]]
            if k >= pr and r > best:
                best, bi = r, k
            flat_s.append(sid_of[s_])
            flat_y.append(max(-128, min(127, yo)))
        mv.append((start, len(seq), bi, best, pr))
    # sources
    h = ['/* Generated by tools/kfm_game_build.py from the user\'s ROM set. Do not distribute. */',
         '#ifndef KFM_GAME_H', '#define KFM_GAME_H',
         '#define KFG_N_POSES %d' % len(meta), '#define KFG_SPAN_BLOCK %dU' % SPAN_BLOCK,
         '#define KFG_SPI_SIZE %dUL' % len(spi),
         '#define KFG_BUILD_ID 0x%08XUL  /* also in KFMG.DAT, bytes 4-7 */' % build_id,
         '#define KFG_FLOOR_Y (%d)' % (d['thomas_poses'] and (193 + kb.LOG_Y + 16) or 0)]
    for name, i in ids.items():
        h.append('#define KFG_%s %d' % (name.upper(), i))
    h.append('#define KFG_THOMAS_WALK_N %d' % len(walk_seq))
    for k, nm in enumerate(mv_names):
        h.append('#define KFG_MV_%s %d' % (nm.upper(), k))
    h += ['#define KFG_N_MOVES %d' % len(mv_names),
          'typedef struct { signed char top; unsigned char rows; unsigned int a0; unsigned int a1;',
          '                 signed char off0; signed char off1; unsigned int n0; unsigned int n1; unsigned int spans;',
          '                 unsigned char bank; } kfg_meta_t;   /* bank: the SPI RAM bank of the streams and spans */',
          'typedef struct { unsigned int start; unsigned char len; unsigned char hit; unsigned char reach; unsigned char press; } kfg_move_t;',
          'extern const unsigned char kfg_native_left[KFG_N_POSES];',
          'extern const kfg_meta_t kfg_meta[KFG_N_POSES];',
          'extern const unsigned char kfg_walk_seq[KFG_THOMAS_WALK_N];',
          'extern const kfg_move_t kfg_moves[KFG_N_MOVES];',
          'extern const unsigned char kfg_move_shape[];',
          'extern const signed char kfg_move_yoff[];',
          'extern const signed char kfg_death_x[];        /* per death frame: the shape\'s left edge from the anchor */',
          '#define KFG_KNIFE_DIE_N %d' % len(cap['knife_die']['frames']),
          'extern const signed char kfg_knife_die[KFG_KNIFE_DIE_N][3];  /* per frame: pose, x (away from Thomas: < 0), feet below the floor */',
          '#endif']
    (gen / 'kfm_game.h').write_text('\n'.join(h) + '\n')
    c = ['/* Generated by tools/kfm_game_build.py. */', '#include <avr/pgmspace.h>', '#include "kfm_game.h"',
         'const kfg_meta_t kfg_meta[KFG_N_POSES] PROGMEM = {']
    for top, rows, (a0, o0, n0), (a1, o1, n1), sp, bank in meta:
        c.append('  {%d,%d,0x%04X,0x%04X,%d,%d,%d,%d,0x%04X,%d},' % (top, rows, a0, a1, o0, o1, n0, n1, sp, bank))
    c.append('};')
    c.append('const unsigned char kfg_walk_seq[KFG_THOMAS_WALK_N] PROGMEM = {%s};' %
             ','.join(str(ids['thomas_walk%d' % i]) for i in walk_seq))
    c.append('const kfg_move_t kfg_moves[KFG_N_MOVES] PROGMEM = {%s};' % ','.join('{%d,%d,%d,%d,%d}' % t for t in mv))
    c.append('const unsigned char kfg_native_left[KFG_N_POSES] PROGMEM = {%s};' % ','.join(map(str, native_left)))
    c.append('const unsigned char kfg_move_shape[] PROGMEM = {%s};' % ','.join(map(str, flat_s)))
    c.append('const signed char kfg_move_yoff[] PROGMEM = {%s};' % ','.join(map(str, flat_y)))
    c.append('const signed char kfg_death_x[] PROGMEM = {%s};' % ','.join(str(max(-128, min(127, v))) for v in cap['death_x']))
    c.append('const signed char kfg_knife_die[KFG_KNIFE_DIE_N][3] PROGMEM = {%s};' % ','.join(
        '{%d,%d,%d}' % (ids['knife_die%d' % sh], max(-128, min(127, dx)), max(-128, min(127, dy)))
        for sh, dx, dy in cap['knife_die']['frames']))
    (gen / 'kfm_game.c').write_text('\n'.join(c) + '\n')
    for nm, (st, ln, hi, re, pr) in zip(mv_names, mv):
        print('move %-13s %2d frames, pressed on %2d, hits on frame %2d, reach %d px' % (nm, ln, pr, hi, re))
    print('poses facing left natively:', [n for n, f in zip(poses, native_left) if f])


if __name__ == '__main__':
    main()
