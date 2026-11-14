#!/usr/bin/env python3
"""Stage 1: pull Kung-Fu Master floor 1 data out of the user's ROM set.

Runs STEArcade's reference machine (not part of this repository: pass the path of a
checkout with --stearcade) on the user's own MAME `kungfum.zip`, and records:

  * the floor's world map, built by calling the game's own column generator ($5765)
    for each of the floor's 224 columns (rows 6-31 of the tilemap);
  * the score panel's tilemap (rows 0-5) at the start of play;
  * the sprite poses of Thomas walking and of the grippers walking / holding, taken
    from the game's sprite RAM while Thomas walks into the first grippers;
  * a pen histogram of the playfield and of the panel (for the palette fit).

Output: build/kfm_extract.pkl (ROM-derived: never commit or distribute it).
"""
import argparse, collections, pickle, sys, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

NEEDED = ['a-4e-c.bin', 'a-4d-c.bin', 'g-4c-a.bin', 'g-4d-a.bin', 'g-4e-a.bin', 'g-1j-.bin', 'g-1f-.bin',
          'g-1h-.bin', 'b-1m-.bin', 'b-1n-.bin', 'b-1l-.bin', 'b-5f-.bin', 'b-4k-.bin', 'b-4f-.bin', 'b-4l-.bin',
          'b-4h-.bin', 'b-3n-.bin', 'b-4n-.bin', 'b-4m-.bin', 'b-3m-.bin', 'b-4c-.bin', 'b-4e-.bin', 'b-4d-.bin',
          'b-4a-.bin',
          'a-3e-.bin', 'a-3f-.bin', 'a-3h-.bin']     # the sound board: ADPCM samples (3e, 3f) and its program (3h)

THOMAS_X, THOMAS_Y = 120, 193     # Thomas's anchor on the arcade screen while the floor scrolls


def setup(stearcade, romzip, build):
    out = build / 'rom'
    (out / 'kungfum').mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(romzip) as z:
        names = set(z.namelist())
        missing = [n for n in NEEDED if n not in names]
        if missing:
            sys.exit('%s lacks %s (MAME set kungfum expected)' % (romzip, ', '.join(missing)))
        for n in NEEDED:
            (out / 'kungfum' / n).write_bytes(z.read(n))
    st = Path(stearcade)
    if not (st / 'games/kungfum/tools/machine.py').exists():
        sys.exit('%s is not an STEArcade checkout (games/kungfum/tools/machine.py missing)' % st)
    sys.path[:0] = [str(st / 'games/kungfum/tools'), str(st / 'engine')]
    import paths
    paths.EXTRACTED = out                     # our copy of the ROMs, before machine.py imports the constant
    import machine
    machine.EXTRACTED = out
    return machine


def boot_to_play(machine):
    """coin, start, run until the floor's play state ($E000 = 4)"""
    m = machine.KungFuPort()
    for f in range(3000):
        sysb = 0xFF
        if 300 <= f < 304:
            sysb &= ~0x08
        if 360 <= f < 364:
            sysb &= ~0x01
        m.system, m.p1 = sysb, 0xFF
        m.run_frame_port()
        if m.mem[0xE000] == 4 and f > 800:
            return m, f
    raise RuntimeError('play never started')


def gen_columns(m, floor=0, ncols=0xE0):
    """call the game's column generator ($5765, A = column) on a copy of the RAM for every column"""
    mem0 = bytes(m.mem)
    cols = []
    for col in range(ncols):
        m.mem[:] = mem0
        m.mem[0xE080] = floor
        c = m.cpu
        c.a = col
        c.sp = 0xEEFE
        m.mem[0xEEFE], m.mem[0xEEFF] = 0xF0, 0xFF          # return to a sentinel
        c.pc = 0x5765
        n = 0
        while c.pc != 0xFFF0:
            c.step()
            n += 1
            if n > 200000:
                raise RuntimeError('column %d: no return (pc %04X)' % (col, c.pc))
        ring = (col + 0x20) & 0x3F
        cols.append([(m.mem[0xD000 + r * 64 + ring], m.mem[0xD800 + r * 64 + ring]) for r in range(6, 32)])
    m.mem[:] = mem0
    return cols


def objects(sr, H):
    """sprite RAM -> objects, each a list of entries (colour, code, flipx, flipy, cells, sx, sy);
    sx, sy: screen position (x 0-255 visible, line 0-255) of the top-left of the topmost cell."""
    ents = []
    for o in range(0, 256, 8):
        code = sr[o + 4] + ((sr[o + 5] & 7) << 8)
        if code == 0:
            continue
        col = sr[o] & 0x1F
        sx = 256 * (sr[o + 7] & 1) + sr[o + 6] - 128
        sy = 256 + 128 - 15 - (256 * (sr[o + 3] & 1) + sr[o + 2])
        fx, fy = (sr[o + 5] >> 6) & 1, (sr[o + 5] >> 7) & 1
        i = H[(code >> 5) & 0x1F]
        n = 1
        if i == 1:
            code &= ~1
            sy -= 16
            n = 2
        elif i == 2:
            code &= ~3
            sy -= 48
            n = 4
        ents.append((col, code, fx, fy, n, sx, sy))
    objs, cur = [], []
    for e in ents:                     # a gripper starts at its head (colour 5); Thomas comes first
        if e[0] == 5 and cur:
            objs.append(cur)
            cur = []
        cur.append(e)
    if cur:
        objs.append(cur)
    return objs


def pose_of(obj, ax, ay):
    return tuple((col, code, fx, fy, n, sx - ax, sy - ay) for col, code, fx, fy, n, sx, sy in obj)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stearcade', default=str(ROOT / 'third_party' / 'stearcade'), help='path of an STEArcade checkout')
    ap.add_argument('--rom', required=True, help="your MAME kungfum.zip")
    ap.add_argument('--build', default=str(Path(__file__).resolve().parents[1] / 'build'))
    a = ap.parse_args()
    build = Path(a.build)
    machine = setup(a.stearcade, a.rom, build)
    import numpy as np
    v = machine.Video()
    H = v.height

    m, f0 = boot_to_play(machine)
    print('play starts at frame %d, scroll %d' % (f0, m.scroll))
    # the unscrolled panel: screen x 0-255 shows tilemap x 128-383 (columns 16-47)
    panel = [[(m.mem[0xD000 + r * 64 + 16 + c], m.mem[0xD800 + r * 64 + 16 + c]) for c in range(32)] for r in range(6)]
    cols = gen_columns(m, 0)
    print('floor 1: %d columns' % len(cols))

    thomas_poses, grip_poses = collections.OrderedDict(), collections.OrderedDict()
    thomas_seq = []
    hist_pf, hist_panel = collections.Counter(), collections.Counter()
    for k in range(700):
        m.system, m.p1 = 0xFF, 0xFF ^ (0x02 if k > 10 else 0)     # hold left
        m.mem[0xE084] = 6
        m.mem[0xE709] = 0x3F                                      # infinite energy (MAME's cheat)
        m.run_frame_port()
        objs = objects(m.sprite_ram, H)
        rest = objs
        if objs and objs[0][0][0] != 5:
            p = pose_of(objs[0], THOMAS_X, THOMAS_Y)
            thomas_poses.setdefault(p, len(thomas_poses))
            thomas_seq.append((m.scroll, thomas_poses[p]))
            rest = objs[1:]
        for o in rest:
            if o[0][0] == 5:
                grip_poses.setdefault(pose_of(o, o[0][5], o[0][6]), len(grip_poses))
        if k % 3 == 0:
            raw = v.render_raw(m.mem, m.sprite_ram, m.scroll)[:, 128:384]
            u, c = np.unique(raw[57:, 32:224], return_counts=True)
            hist_pf.update(dict(zip(u.tolist(), c.tolist())))
            u, c = np.unique(raw[:48], return_counts=True)
            hist_panel.update(dict(zip(u.tolist(), c.tolist())))
    print('Thomas poses %d, gripper poses %d' % (len(thomas_poses), len(grip_poses)))
    out = dict(cols=cols, panel=panel, thomas_poses=list(thomas_poses), thomas_seq=thomas_seq,
               grip_poses=list(grip_poses), hist_pf=hist_pf, hist_panel=hist_panel,
               tiles=np.array(v.tiles), sprites=np.array(v.sprites), pens=np.array(v.pens))
    (build / 'kfm_extract.pkl').write_bytes(pickle.dumps(out))
    print('wrote', build / 'kfm_extract.pkl')


if __name__ == '__main__':
    main()
