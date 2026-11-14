#!/usr/bin/env python3
"""Check the custom blitter's RAM tiles byte for byte against a Python model.

Runs the prototype frozen (no autoplay, no grippers), reads Thomas's pose, the
camera, the VRAM rows, the RAM tiles and the SPI RAM background map through
cuzebox-ex's API, then rebuilds every cell Thomas covers (ROM tile + pose pixels)
and compares. Usage: python3 tools/verify_blit.py [--frames N] [--flip]
"""
import argparse, re, subprocess, sys, time
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from run_test import Api, symbols, CEX

THOMAS_X, THOMAS_Y = 120 - 32, 193 - 24


def tables():
    t = (ROOT / 'gen' / 'kfm_tables.c').read_text()
    arr = lambda n: re.search(n + r'\[\] PROGMEM = \{(.*?)\};', t, re.S).group(1)
    ps = [tuple(int(x, 0) for x in e.split(',')) for e in re.findall(r'\{([^{}]*)\}', arr('kfb_pstreams'))]
    info = [(int(b), int(c)) for a, b, c in re.findall(r'\{\{([^}]*)\},(-?\d+),(\d+)\}', arr('kfb_span_info'))]
    seq = [int(x) for x in arr('kfm_thomas_seq').split(',')]
    base = [int(x) for x in arr('kfb_pose_base').split(',')]
    flip = [int(x) for x in arr('kfb_pose_flip').split(',')]
    return ps, info, seq, base, flip


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=40)
    ap.add_argument('--port', type=int, default=24840)
    ap.add_argument('--right', action='store_true', help='face right (flipped poses)')
    ap.add_argument('--dump', action='store_true')
    ap.add_argument('--warm', type=int, default=320, help='frames of autoplay before freezing')
    a = ap.parse_args()
    sym = symbols(str(ROOT / '_bin_' / 'kfm748.elf'))
    S = lambda n: sym[n] - 0x800000
    ps, info, seq, base_of, flip_of = tables()
    rom_c = (ROOT / 'gen' / 'kfm_rom.c').read_text().split('{', 1)[1]
    blob = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-F]{2})', rom_c))
    hdr = (ROOT / 'gen' / 'kfm_rom.h').read_text()
    rom_base = int(re.search(r'KFM_ROM_BASE\s+0x([0-9A-F]+)', hdr).group(1), 16)
    dat = (ROOT / 'build' / 'KFM.DAT').read_bytes()

    p = subprocess.Popen([CEX, '--headless', '--paused', '--unthrottled', '--api-port', str(a.port), 'kfm748.uze'],
                         cwd=str(ROOT / '_bin_'), stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            try:
                api = Api(a.port)
                break
            except OSError:
                time.sleep(0.1)
        def rd(addr, n, region='SRAM'):
            out = b''
            while n > 0:
                k = min(n, 256)
                out += bytes(api.cmd('READ_MEM %s %d %d' % (region, addr, k))['values'])
                addr += k
                n -= k
            return out
        wr = lambda name, v: api.cmd('WRITE_MEM SRAM %d %d' % (S(name), v))

        def run(n):
            api.cmd('RUN_FRAMES %d' % n)
            while int(api.cmd('GET_STATE')['run_frames_remaining']):
                time.sleep(0.01)
        run(a.warm)
        wr('bench_ngrip', 0)
        wr('bench_engine', 1)
        if a.right:
            wr('facing_left', 0)
        wr('bench_auto', 0)
        run(a.frames)
        # stop in the main loop's wait, after a complete update
        api.cmd('BREAK SET %d' % (sym['post_display'] // 2))
        api.cmd('RUN_FRAMES 2')
        for _ in range(400):
            if api.cmd('RUN_STATUS').get('break_hit'):
                break
            time.sleep(0.005)
        cam = int.from_bytes(rd(S('cam'), 2), 'little', signed=True)
        walk = rd(S('walk_phase'), 1)[0]
        left = rd(S('facing_left'), 1)[0]
        pose = (0 if left else 4) + seq[walk]
        pf = rd(S('pfrow'), 30 * 25)
        rtb = rd(S('m74_rtbase'), 1)[0]
        ram = rd(0x100, 0xE00)          # data space 0x100 - 0xEFF
    finally:
        try:
            api.cmd('QUIT')
        except Exception:
            pass
        try:
            p.wait(5)
        except Exception:
            p.kill()

    b = base_of[pose]
    fl = flip_of[pose]
    vx = THOMAS_X + (cam & 7)
    a0, a1, o0, o1 = ps[b][:4]
    top, rows = info[b]
    P = vx & 1
    addr, off = (a1, o1) if P else (a0, o0)
    # expected sprite pixels in VRAM pixel / logical line coordinates
    pix = {}
    q = addr
    for r in range(rows):
        st, ln = dat[q], dat[q + 1]
        data = dat[q + 2:q + 2 + ln]
        q += 2 + ln
        for j, bt in enumerate(data):
            for h, nyb in ((0, bt >> 4), (1, bt & 15)):
                if nyb:
                    x = vx + off + 2 * (st + j) + h
                    if fl:
                        x = 2 * vx + 15 - x          # mirror about anchor + 7.5
                    pix[(THOMAS_Y + top + r, x)] = nyb
    cells = sorted({(y >> 3, x >> 3) for y, x in pix})
    bad, checked = 0, 0
    for tr, tc in cells:
        if not (4 <= tr <= 28 and 0 <= tc <= 24):
            continue
        row = pf[(tr - 4) * 30:(tr - 3) * 30]
        v = row[5 + tc]
        bgi = bgmap[(tr - 4) * 224 + (row[1] | row[2] << 8) - 0x100 - (tr - 4) * 224 + tc] \
            if False else bytes(dat[(row[1] | row[2] << 8) + tc:(row[1] | row[2] << 8) + tc + 1])[0]
        t0, t1 = row[3], row[4]
        taddr = (t0 << 8) + bgi * 32 if bgi < 128 else (t1 << 8) + (bgi - 128) * 32
        rom = blob[taddr - rom_base:taddr - rom_base + 32]
        exp = np.array([[(rom[yy * 4 + xx // 2] >> (4 if xx % 2 == 0 else 0)) & 15 for xx in range(8)] for yy in range(8)])
        for yy in range(8):
            for xx in range(8):
                k = (tr * 8 + yy, tc * 8 + xx)
                if k in pix:
                    exp[yy, xx] = pix[k]
        if not (v & 0x80):
            front = blob[(re.search(r'KFM_MASKIDX\s+0x([0-9A-F]+)', hdr) and 0)] if False else None
            print('cell (%d,%d): not a RAM tile (VRAM %02X)' % (tr, tc, v))
            bad += 1
            continue
        ra = 0x100 + (v & 0x7F) * 32 - 0x100
        got = np.array([[(ram[ra + yy * 4 + xx // 2] >> (4 if xx % 2 == 0 else 0)) & 15 for xx in range(8)]
                        for yy in range(8)])
        checked += 1
        if (got != exp).any():
            bad += 1
            if bad <= 4:
                print('cell (%d,%d) RAM tile %d differs:' % (tr, tc, v & 0x7F))
                for yy in range(8):
                    print('   got %s   exp %s' % (''.join('%x' % c for c in got[yy]), ''.join('%x' % c for c in exp[yy])))
    if a.dump:
        ys = sorted({y for y, x in pix}); xs = sorted({x for y, x in pix})
        X0, X1 = xs[0] & ~7, (xs[-1] | 7) + 1
        for y in ys:
            g = ''
            e = ''
            for x in range(X0, X1):
                tr, tc = y >> 3, x >> 3
                row = pf[(tr - 4) * 30:(tr - 3) * 30]
                v = row[5 + tc]
                if v & 0x80:
                    ra = (v & 0x7F) * 32
                    c = (ram[ra + (y & 7) * 4 + (x & 7) // 2] >> (4 if x % 2 == 0 else 0)) & 15
                    g += '%x' % c
                else:
                    g += '.'
                if (y, x) in pix:
                    e += '%x' % pix[(y, x)]
                else:
                    brow = pf[(tr - 4) * 30:(tr - 3) * 30]
                    bga = brow[1] | brow[2] << 8
                    bgi = dat[bga + tc]
                    ta = (brow[3] << 8) + bgi * 32 if bgi < 128 else (brow[4] << 8) + (bgi - 128) * 32
                    rb = blob[ta - rom_base + (y & 7) * 4 + (x & 7) // 2]
                    e += '%x' % ((rb >> (4 if x % 2 == 0 else 0)) & 15)
            print('%3d  %s   %s' % (y, g, e))
    print('pose %d (base %d, flip %d), cam %d, vx %d: %d cells, %d checked, %d wrong' %
          (pose, b, fl, cam, vx, len(cells), checked, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
