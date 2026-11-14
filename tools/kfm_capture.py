#!/usr/bin/env python3
"""Stage 1b: capture the sprite poses of a playable floor 1 from the user's ROM set.

Uses STEArcade's reference machine (like kfm_extract.py) and records:
  * Thomas's moves exactly as the arcade animates them: each action tapped from
    standing, one pose per frame until he stands again (punch, kick, crouch, crouch
    punch, crouch kick, jump, jump kick);
  * the enemies' poses on floor 1, by colour set: grippers (4, 5), knife throwers (6),
    the boss (8, 9) and the knife (7). A simple bot plays with infinite energy and a
    frozen timer; Thomas is moved to the knife throwers' stretch and to the boss's
    area the way MAME's position cheats do.

Each enemy pose comes with (times seen, times seen to Thomas's left), which tells the
way it faces. Poses are tuples of sprite entries (colour, code, flipx, flipy, cells, dx, dy):
  Thomas: dx from his screen anchor (x 120), dy from his feet;
  enemies: dx from the object's first entry, dy from its feet.
The emulation is deterministic, so the same ROM set gives the same pose order.

Output: build/kfm_capture.pkl (ROM-derived: never commit or distribute it).
Takes a few minutes (the reference Z80 runs in Python).
"""
import argparse, collections, pickle, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import kfm_extract

RIGHT, LEFT, DOWN, UP, PUNCH, KICK = 0x01, 0x02, 0x04, 0x08, 0x20, 0x80   # (0x80 kicks, 0x20 punches)
# each move: the input script, and the frame on which its action button is pressed
MOVES = {'kick': ([(KICK, 3)], 0), 'punch': ([(PUNCH, 3)], 0),
         'crouch': ([(DOWN, 30)], 0),
         'crouch_kick': ([(DOWN, 6), (DOWN | KICK, 3), (DOWN, 30)], 6),
         'crouch_punch': ([(DOWN, 6), (DOWN | PUNCH, 3), (DOWN, 30)], 6),
         'jump': ([(UP, 3)], 0), 'jump_kick': ([(UP, 3), (0, 8), (KICK, 3)], 11)}
CLASSES = {(4, 5): 'grip', (6,): 'knife', (8, 9): 'boss', (6, 7): 'knife_hold', (7,): 'blade'}
POPS = 12      # score pop-ups and hit sparks: their own objects


def entries(sr, H):
    out = []
    for o in range(0, 256, 8):
        code = sr[o + 4] + ((sr[o + 5] & 7) << 8)
        if code == 0:
            continue
        col = sr[o] & 0x1F
        fx, fy = (sr[o + 5] >> 6) & 1, (sr[o + 5] >> 7) & 1
        sx = 256 * (sr[o + 7] & 1) + sr[o + 6] - 128
        sy = 256 + 128 - 15 - (256 * (sr[o + 3] & 1) + sr[o + 2])
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
        out.append((o // 8, col, code, fx, fy, n, sx, sy))
    return out


def objects(ents):
    """consecutive sprite slots close together and of one kind form an object"""
    objs, cur = [], []
    for e in ents:
        if cur and (e[0] != cur[-1][0] + 1 or abs(e[6] - cur[-1][6]) > 20 or (e[1] < 3) != (cur[-1][1] < 3) or
                    (e[1] == 5 and cur[-1][1] != 5 and cur[-1][1] >= 3)):
            objs.append(cur)
            cur = []
        cur.append(e)
    if cur:
        objs.append(cur)
    return objs


def capture_knife_death(snap, H):
    """A knife thrower's death, frame by frame: from the hit (his 500 / 800 pop-up appears) until he
    has fallen off the screen. A bot plays the knife throwers' stretch: walks up to the nearest enemy,
    punches and kicks, shakes grippers off. Kept: the first death with the camera still. Returned in
    one orientation (he falls to the left, facing right; the game mirrors it): per frame the shape
    (entries relative to a smooth anchor and his feet), the anchor's x from his left edge just before
    the hit, and his feet below the floor."""
    RIGHT_, LEFT_, KICK_, PUNCH_ = 0x01, 0x02, 0x80, 0x20
    m = pickle.loads(snap)

    def step(p1):
        m.system, m.p1 = 0xFF, p1
        m.mem[0xE084] = 6
        m.mem[0xE709] = 0x3F
        m.mem[0xE002] = 0x19
        m.run_frame_port()

    def scr():
        return m.mem[0xE712] | m.mem[0xE713] << 8

    while scr() > 1230 * 32:
        x = scr() - 48
        m.mem[0xE712], m.mem[0xE713] = x & 0xFF, x >> 8
        step(0xFF ^ LEFT_)
    last_sc, stall, prev, track = None, 0, None, None
    for f in range(12000):
        ents = entries(m.sprite_ram, H)
        th = [e for e in ents if e[1] < 3]
        en = [e for e in ents if e[1] in (4, 5, 6)]
        tx = sum(e[6] for e in th) / len(th) if th else 120
        stall = stall + 1 if m.scroll == last_sc else 0
        last_sc = m.scroll
        p1 = 0xFF
        near = sorted(en, key=lambda e: abs(e[6] + 8 - tx))
        if any(e[1] in (4, 5) and abs(e[6] + 8 - tx) < 20 for e in en) and stall > 20:
            p1 ^= LEFT_ if f % 2 else RIGHT_                            # held: shake off
        elif near and abs(near[0][6] + 8 - tx) < 70:
            d = near[0][6] + 8 - tx
            if abs(d) > 22:
                p1 ^= RIGHT_ if d > 0 else LEFT_                         # walk up to him
            elif f % 8 == 0:
                p1 ^= RIGHT_ if d > 0 else LEFT_
            elif f % 8 in (2, 3):
                p1 ^= KICK_ if (f // 8) % 2 else PUNCH_
        else:
            p1 ^= LEFT_
        kn_before = [e for e in entries(m.sprite_ram, H) if e[1] == 6]
        step(p1)
        if scr() < 300 * 32 or scr() > 1500 * 32:                        # stay in the stretch
            m.mem[0xE712], m.mem[0xE713] = (1230 * 32) & 0xFF, (1230 * 32) >> 8
        ents = entries(m.sprite_ram, H)
        kn = [e for e in ents if e[1] == 6]
        if track is None and kn and kn_before and any(e[1] == POPS and e[2] in (755, 757) for e in ents):
            track = dict(scroll=m.scroll, before=kn_before, frames=[])
        if track is not None:
            if not kn:
                if len(track['frames']) > 10:
                    break                                               # captured
                track = None
                continue
            if m.scroll != track['scroll'] or len(track['frames']) > 60:
                track = None                                            # the camera moved: try another
                continue
            track['frames'].append([(e[1], e[2], e[3], e[4], e[5], e[6], e[7]) for e in kn])
    else:
        raise SystemExit('a knife thrower\'s death was not captured')
    fr = track['frames']
    ground = feet([(e[1], e[2], e[3], e[4], e[5], e[6], e[7]) for e in track['before']])
    left0 = min(e[6] for e in track['before'])
    if min(e[5] for e in fr[-1]) > min(e[5] for e in fr[0]):      # he fell to the right: mirror it all
        M = 2 * 128
        mir = lambda es: [(c, code, fx ^ 1, fy, n, M - x - 16, y) for c, code, fx, fy, n, x, y in es]
        fr = [mir(es) for es in fr]
        bx = [M - e[6] - 16 for e in track['before']]
        left0 = min(bx)
    # shapes; a smooth anchor: each shape's offset chosen so the anchor keeps its pace at a change
    keys, sid, x0s = [], [], []
    for es in fr:
        x0, y0 = min(e[5] for e in es), min(e[6] for e in es)
        k = tuple(sorted((c, code, fx, fy, n, x - x0, y - y0) for c, code, fx, fy, n, x, y in es))
        if k not in keys:
            keys.append(k)
        sid.append(keys.index(k))
        x0s.append(x0)
    off = {sid[0]: 0}
    anc = []
    for i, x0 in enumerate(x0s):
        s_ = sid[i]
        if s_ not in off:
            k = min(3, i - 1)
            pace = (anc[i - 1] - anc[i - 1 - k]) / float(k) if k > 0 else 0.0
            off[s_] = x0 - int(round(anc[i - 1] + pace))
        anc.append(x0 - off[s_])
    shapes = []
    for k_ in keys:
        sh = [e for e in k_]
        shapes.append(None)
    out = []
    for i, es in enumerate(fr):
        b = feet(es)
        rel = tuple((c, code, fx, fy, n, x - anc[i], y - b) for c, code, fx, fy, n, x, y in es)
        if shapes[sid[i]] is None:
            shapes[sid[i]] = rel
        out.append((sid[i], anc[i] - left0, b - ground))
    print('knife thrower death: %d frames, %d shapes; drift %d px, drop %d px' %
          (len(out), len(shapes), out[-1][1] - out[0][1], out[-1][2]))
    return dict(shapes=shapes, frames=out)


def feet(pose_abs):
    return max(e[6] + 16 * e[4] for e in pose_abs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stearcade', default=str(ROOT / 'third_party' / 'stearcade'),
                    help='STEArcade (default: the bundled subset in third_party/stearcade)')
    ap.add_argument('--rom', required=True)
    ap.add_argument('--build', default=str(ROOT / 'build'))
    a = ap.parse_args()
    build = Path(a.build)
    machine = kfm_extract.setup(a.stearcade, a.rom, build)
    H = machine.Video().height

    # ---- Thomas's moves -------------------------------------------------------------
    m, _ = kfm_extract.boot_to_play(machine)
    snap = pickle.dumps(m)

    def thomas(mm):
        # all of Thomas's entries (colours 0-2): some moves put parts of him in separate slots
        o = [e for e in entries(mm.sprite_ram, H) if e[1] < 3]
        if not o:
            return None
        b = feet([(e[1], e[2], e[3], e[4], e[5], e[6], e[7]) for e in o])
        return tuple((e[1], e[2], e[3], e[4], e[5], e[6] - 120, e[7] - b) for e in o), b

    stand_pose, stand_feet = thomas(pickle.loads(snap))
    moves = {'stand': [(stand_pose, 0)]}
    press = {}
    for name, (script, pr) in MOVES.items():
        press[name] = pr
        mm = pickle.loads(snap)
        inputs = []
        for mask, n in script:
            inputs += [mask] * n
        inputs += [0] * 60
        seq = []
        for mask in inputs:
            mm.system, mm.p1 = 0xFF, 0xFF ^ mask
            mm.mem[0xE709] = 0x3F
            mm.run_frame_port()
            t = thomas(mm)
            if t:
                seq.append((t[0], t[1] - stand_feet))   # (shape, feet offset: < 0 is up)
        while len(seq) > 1 and seq[-1] == seq[-2] and seq[-1][0] == stand_pose:
            seq.pop()
        moves[name] = seq
        print('Thomas %-12s %2d frames, %d shapes' % (name, len(seq), len({s for s, _ in seq})))

    # Thomas's death: his energy emptied, from his first frame out of the standing pose until
    # he has fallen off the bottom of the screen
    mm = pickle.loads(snap)
    for _ in range(20):
        mm.system, mm.p1 = 0xFF, 0xFF
        mm.run_frame_port()
    mm.mem[0xE709] = 0
    seq, started = [], False
    for _ in range(600):
        mm.system, mm.p1 = 0xFF, 0xFF
        mm.run_frame_port()
        t = thomas(mm)
        if t is None:
            if started:
                break
            continue
        if not started and t[0] == stand_pose:
            continue
        started = True
        if t[1] - stand_feet > 100:
            break
        ax = min(e[5] for e in t[0])            # he drifts sideways: keep the shape, and the drift apart
        seq.append((tuple((c, code, fx, fy, n, dx - ax, dy) for c, code, fx, fy, n, dx, dy in t[0]),
                    t[1] - stand_feet, ax))
    if not seq:
        raise SystemExit('Thomas\'s death was not captured')
    moves['death'] = [(sh, yo) for sh, yo, ax in seq]     # (shape, feet offset: > 0 is down)
    death_x = [ax for sh, yo, ax in seq]                   # the shape's left edge, from Thomas's anchor
    print('Thomas %-12s %2d frames, %d shapes' % ('death', len(seq), len({sh for sh, _, _ in seq})))

    # ---- enemies ---------------------------------------------------------------------
    m = pickle.loads(snap)
    mem = {}
    poses = collections.defaultdict(lambda: collections.OrderedDict())
    f = [0]

    def step(p1):
        m.system, m.p1 = 0xFF, p1
        m.mem[0xE084] = 6
        m.mem[0xE709] = 0x3F                 # infinite energy
        m.mem[0xE002] = 0x19                 # the timer stays above 1900
        m.run_frame_port()
        f[0] += 1
        if m.mem[0xE000] == 4 and m.mem[0xE080] == 0:
            te = [e[6] for e in entries(m.sprite_ram, H) if e[1] < 3]
            tx = (sum(te) / len(te) + 8) if te else None
            ents = entries(m.sprite_ram, H)
            boss = [e for e in ents if e[1] in (8, 9)]   # one boss at a time: all his sprites, even
            if boss:                                       # when lying wide (grouping would split him)
                b = feet([(e[1], e[2], e[3], e[4], e[5], e[6], e[7]) for e in boss])
                ax = boss[0][6]
                p = tuple((e[1], e[2], e[3], e[4], e[5], e[6] - ax, e[7] - b) for e in boss)
                c = poses['boss'].get(p, [0, 0])
                c[0] += 1
                if tx is not None and ax + 8 < tx:
                    c[1] += 1
                poses['boss'][p] = c
            for o in objects(ents):
                if any(e[1] in (8, 9) for e in o):
                    continue                               # (the boss: above)
                pop = [e for e in o if e[1] == POPS]
                if pop:                                   # score pop-ups: anchored on their first entry, top-left
                    ax, ay = pop[0][6], min(e[7] for e in pop)
                    p = tuple((e[1], e[2], e[3], e[4], e[5], e[6] - ax, e[7] - ay) for e in pop)
                    c = poses['pop'].get(p, [0, 0])
                    c[0] += 1
                    poses['pop'][p] = c
                o = [e for e in o if e[1] != POPS]        # score pop-ups and sparks are separate
                if not o:
                    continue
                cls = tuple(sorted(set(e[1] for e in o)))
                if cls in CLASSES:
                    b = feet([(e[1], e[2], e[3], e[4], e[5], e[6], e[7]) for e in o])
                    ax = o[0][6]
                    p = tuple((e[1], e[2], e[3], e[4], e[5], e[6] - ax, e[7] - b) for e in o)
                    d = poses[CLASSES[cls]]
                    c = d.get(p, [0, 0])
                    c[0] += 1
                    if tx is not None and ax + 8 < tx:
                        c[1] += 1               # seen to Thomas's left: facing right
                    d[p] = c

    def bot():
        sr = m.sprite_ram
        p1 = 0xFF
        ents = [(sr[o] & 0x1F, 256 * (sr[o + 7] & 1) + sr[o + 6] - 128) for o in range(0, 256, 8)
                if sr[o + 4] or sr[o + 5] & 7]
        tx = [x for c, x in ents if c < 3]
        ex = [x for c, x in ents if c >= 3]
        t = sum(tx) / len(tx) if tx else 120
        front = [x for x in ex if t - 56 < x < t]
        behind = [x for x in ex if t < x < t + 48]
        sc = m.scroll
        mem['stall'] = (mem.get('stall', 0) + 1 if sc == mem.get('sc') else 0) if mem.get('stall', 0) < 160 else 0
        mem['sc'] = sc
        if mem['stall'] > 40:
            p1 ^= LEFT if (f[0] // 2) % 2 else RIGHT       # shake grippers off
        elif behind and not front:
            p1 ^= RIGHT
            if f[0] % 10 < 3:
                p1 ^= KICK if (f[0] // 10) % 2 else PUNCH
        elif front:
            p1 ^= LEFT if f[0] % 30 < 4 else 0
            if f[0] % 10 < 3:
                p1 ^= KICK if (f[0] // 10) % 2 else PUNCH
        else:
            p1 ^= LEFT
        return p1

    def move_to(scroll):
        while (m.mem[0xE712] | m.mem[0xE713] << 8) > scroll * 32:
            x = (m.mem[0xE712] | m.mem[0xE713] << 8) - 48
            m.mem[0xE712], m.mem[0xE713] = x & 0xFF, x >> 8
            step(0xFF ^ LEFT)

    for _ in range(900):
        step(bot())                                 # grippers at the start
    move_to(1230)                                   # the knife throwers' stretch
    for _ in range(1500):
        step(bot())
    move_to(380)                                    # the boss
    for _ in range(1800):
        step(bot())
    for k in ('grip', 'knife', 'boss', 'pop'):
        print('%-6s %d poses' % (k, len(poses[k])))

    knife_die = capture_knife_death(snap, H)

    out = dict(moves=moves, press=press, death_x=death_x, knife_die=knife_die, enemies={k: list(v.items()) for k, v in poses.items()})
    (build / 'kfm_capture.pkl').write_bytes(pickle.dumps(out))
    print('wrote', build / 'kfm_capture.pkl')


if __name__ == '__main__':
    main()
