#!/usr/bin/env python3
"""Drive the game build headless in cuzebox-ex: scripted input, screenshots, state.

    python3 tools/play_test.py --script "start;left*600:punch/20" --shots 100,300 [--port N]

Script: ';'-separated steps "buttons*frames", buttons '+'-joined from
left right up down b y a x start select; ":punch/N" adds a tap of B every N frames.
"""
import argparse, struct, subprocess, sys, time, json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from run_test import Api, symbols, to192, CEX
BTN = dict(b=1 << 0, y=1 << 1, select=1 << 2, start=1 << 3, up=1 << 4, down=1 << 5, left=1 << 6, right=1 << 7,
           a=1 << 8, x=1 << 9)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--script', default='start;left*600')
    ap.add_argument('--shots', default='')
    ap.add_argument('--port', type=int, default=24990)
    ap.add_argument('--cam', type=int, default=None, help='set the camera after the first step (test shortcut)')
    a = ap.parse_args()
    sym = symbols(str(ROOT / '_bin_' / 'kungfum.elf'))
    S = lambda n: sym[n] - 0x800000
    p = subprocess.Popen([CEX, '--headless', '--paused', '--unthrottled', '--api-port', str(a.port), 'kungfum.uze'],
                         cwd=str(ROOT / '_bin_'), stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            try:
                api = Api(a.port)
                break
            except OSError:
                time.sleep(0.1)
        api.cmd('SCREENSHOT_SETTINGS SET Y_SCALE 1')

        def run(n):
            api.cmd('RUN_FRAMES %d' % n)
            while int(api.cmd('GET_STATE')['run_frames_remaining']):
                time.sleep(0.005)
        rd = lambda n, k: bytes(api.cmd('READ_MEM SRAM %d %d' % (S(n), k))['values'])
        run(420)                                    # bootloader, load
        shots = {int(x) for x in a.shots.split(',') if x}
        f = 0
        out = []
        for step in a.script.split(';'):
            tap = None
            if ':' in step:
                step, t = step.split(':')
                tb, tn = t.split('/')
                tap = (BTN[tb] if tb in BTN else BTN['b'] if tb == 'punch' else BTN['a'], int(tn))
            btns, _, n = step.partition('*')
            n = int(n or 4)
            mask = 0
            for b in btns.split('+'):
                if b:
                    mask |= BTN[b]
            for k in range(n):
                m = mask | (tap[0] if tap and k % tap[1] < 2 else 0)
                api.cmd('SET_INPUT 0 %d 1' % m)
                run(1)
                f += 1
                if f in shots:
                    cap = api.cmd('SCREENSHOT_CAPTURE PNG')
                    data, off = b'', 0
                    while off < cap['size']:
                        q = api.cmd('SCREENSHOT_READ %d 4096' % off)
                        data += bytes(q['values'])
                        off += len(q['values'])
                    raw = ROOT / 'build' / 'shots' / ('game_raw_%05d.png' % f)
                    raw.parent.mkdir(parents=True, exist_ok=True)
                    raw.write_bytes(data)
                    to192(raw, ROOT / 'build' / 'shots' / ('game_%05d.png' % f))
            api.cmd('CLEAR_INPUT 0')
            if a.cam is not None:
                api.cmd('WRITE_MEM SRAM %d %d' % (S('cam'), a.cam & 0xFF)); api.cmd('WRITE_MEM SRAM %d %d' % (S('cam') + 1, (a.cam >> 8) & 0xFF))
                a.cam = None
        st = dict(mode=rd('g_mode', 1)[0], energy=rd('g_energy', 1)[0], score=struct.unpack('<I', rd('g_score', 4))[0],
                  time=struct.unpack('<H', rd('g_time', 2))[0], lives=rd('g_lives', 1)[0],
                  cam=struct.unpack('<h', rd('cam', 2))[0], boss=rd('g_boss_on', 1)[0],
                  boss_energy=rd('g_boss_energy', 1)[0], dropped=struct.unpack('<H', rd('kfm_dropped', 2))[0],
                  ents=[(e[0], e[1], struct.unpack('<h', e[2:4])[0]) for e in
                        (rd('ent', 8 * 6)[i * 8:i * 8 + 8] for i in range(6))], err=rd('kfm_stats', 19)[18])
        print(json.dumps(st))
    finally:
        try:
            api.cmd('QUIT')
        except Exception:
            pass
        try:
            p.wait(5)
        except Exception:
            p.kill()


if __name__ == '__main__':
    main()
