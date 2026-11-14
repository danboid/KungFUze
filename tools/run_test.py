#!/usr/bin/env python3
"""Run the prototype headless in cuzebox-ex and report the frame budget.

    python3 tools/run_test.py [--frames N] [--grips 0-6] [--shots f1,f2,...] [--manual]

Reads kfm_stats through the emulator's TCP API (addresses from the ELF via avr-nm),
counts the RAM tiles in use by scanning the VRAM rows, saves screenshots
(192-pixel samples of the cycle-exact capture) to build/shots/.
"""
import argparse, os, socket, struct, subprocess, sys, time, json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CEX = os.environ.get('CUZEBOX', str(ROOT.parent / 'cuzebox-ex' / 'cuzebox'))


class Api:
    def __init__(self, port):
        self.s = socket.create_connection(('127.0.0.1', port), timeout=120)
        self.f = self.s.makefile('rb')
        self.f.readline()                                   # hello

    def cmd(self, text):
        self.s.sendall(text.encode() + b'\n')
        return json.loads(self.f.readline())


def symbols(elf):
    out = {}
    for line in subprocess.run(['avr-nm', '-S', elf], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) >= 3:
            out[p[-1]] = int(p[0], 16)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=600)
    ap.add_argument('--grips', type=int, default=6)
    ap.add_argument('--nodraw', action='store_true')
    ap.add_argument('--nomask', action='store_true')
    ap.add_argument('--nothomas', action='store_true')
    ap.add_argument('--delay', type=int, default=0)
    ap.add_argument('--budget', type=int, default=0)
    ap.add_argument('--engine', type=int, default=1, help='0: Mode 748 blitter, 1: custom')
    ap.add_argument('--deadline', type=int, default=0)
    ap.add_argument('--still', action='store_true', help='autoplay off: nothing moves')
    ap.add_argument('--calloc', action='store_true', help='C allocator instead of kfb_alloc')
    ap.add_argument('--shots', default='')
    ap.add_argument('--port', type=int, default=24731)
    ap.add_argument('--trace', type=int, default=0, help='sample stats every N frames')
    a = ap.parse_args()
    uze = ROOT / '_bin_' / 'kfm748.uze'
    sym = symbols(str(ROOT / '_bin_' / 'kfm748.elf'))
    sram = lambda name: sym[name] - 0x800000
    p = subprocess.Popen([CEX, '--headless', '--paused', '--unthrottled', '--api-port', str(a.port), str(uze)],
                         cwd=str(uze.parent), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            try:
                api = Api(a.port)
                break
            except OSError:
                time.sleep(0.1)
        api.cmd('SCREENSHOT_SETTINGS SET Y_SCALE 1')

        def rd(addr, n):
            return bytes(api.cmd('READ_MEM SRAM %d %d' % (addr, n))['values'])

        def wr(addr, v):
            return api.cmd('WRITE_MEM SRAM %d %d' % (addr, v))

        def run(n):
            api.cmd('RUN_FRAMES %d' % n)
            while True:
                st = api.cmd('GET_STATE')
                if int(st.get('run_frames_remaining', 0)) == 0 and int(st.get('paused', 0)):
                    return st
                time.sleep(0.01)

        def stats():
            b = rd(sram('kfm_stats'), 23)
            return dict(zip(['updates', 'fields', 'cpu', 'cpu_max', 'cpu_sum', 'late', 'late_count', 'blits',
                             'blits_max', 'rtbase', 'rtmax', 'vblank_lines', 'err', 'rt_used', 'rt_max', 'rt_full'],
                            struct.unpack('<HHBBIBHBBBBBBBBH', b)))

        def ram_tiles():
            n = 0
            for name, rows in (('pfrow', 25), ('hudrow', 2)):
                b = rd(sram(name), 30 * rows)
                for r in range(rows):
                    n += sum(1 for x in b[r * 30 + 5:r * 30 + 30] if x & 0x80)
            return n

        # cuzebox-ex starts in its built-in bootloader, which flashes the game first
        boot = 0
        prev = stats()
        while True:
            run(30)
            boot += 30
            cur = stats()
            # running: our frame counter advanced by one per frame, the layout constant is set
            if cur['vblank_lines'] >= 38 and 28 <= ((cur['fields'] - prev['fields']) & 0xFFFF) <= 31:
                break
            prev = cur
            if boot > 6000:
                raise SystemExit('the game never started (bootloader / SD problem?) %r' % cur)
        r = wr(sram('bench_ngrip'), a.grips)
        if a.nodraw:
            wr(sram('bench_draw'), 0)
        if a.nomask:
            wr(sram('bench_mask'), 0)
        wr(sram('bench_engine'), a.engine)
        if a.calloc:
            wr(sram('bench_calloc'), 1)
        if a.still:
            wr(sram('bench_auto'), 0)
        if a.deadline:
            wr(sram('bench_deadline'), a.deadline)
        if a.budget:
            wr(sram('bench_budget'), a.budget)
        if a.delay:
            wr(sram('bench_delay'), a.delay)
        if a.nothomas:
            wr(sram('bench_thomas'), 0)
        shots = sorted(int(x) for x in a.shots.split(',') if x)
        (ROOT / 'build' / 'shots').mkdir(parents=True, exist_ok=True)
        done, trace = 5, []
        s0 = stats()
        w0 = struct.unpack('<H', rd(sram('kfb_wait_lines'), 2))[0]
        checkpoints = sorted(set(shots + list(range(a.trace, a.frames, a.trace) if a.trace else []) + [a.frames]))
        for f in checkpoints:
            if f > done:
                run(f - done)
                done = f
            if f in shots:
                cap = api.cmd('SCREENSHOT_CAPTURE PNG')
                data, off = b'', 0
                while off < cap['size']:
                    q = api.cmd('SCREENSHOT_READ %d 4096' % off)
                    data += bytes(q['values'])
                    off += len(q['values'])
                raw = ROOT / 'build' / 'shots' / ('raw_%05d.png' % f)
                raw.write_bytes(data)
                to192(raw, ROOT / 'build' / 'shots' / ('shot_%05d.png' % f))
            if a.trace and f % a.trace == 0:
                k = stats()
                trace.append((f, k['cpu'], k['blits'], ram_tiles()))
        s1 = stats()
        dropped = struct.unpack('<H', rd(sram('kfm_dropped'), 2))[0]
        rtdrop = struct.unpack('<H', rd(sram('kfb_rt_drop'), 2))[0]
        waited = struct.unpack('<H', rd(sram('kfb_wait_lines'), 2))[0]
        nu = (s1['updates'] - s0['updates']) & 0xFFFF
        nf = (s1['fields'] - s0['fields']) & 0xFFFF
        res = dict(boot_frames=boot, fields=nf, updates=nu, updates_per_s=round(59.94 * nu / max(nf, 1), 1),
                   engine=('custom' if a.engine else 'm74'), deadline=a.deadline, budget=a.budget, grips=a.grips, draw=not a.nodraw, mask=not a.nomask, thomas=not a.nothomas, budget_lines=s1['vblank_lines'],
                   cpu_lines_avg=round((s1['cpu_sum'] - s0['cpu_sum']) / max(nu, 1), 1), cpu_lines_max=s1['cpu_max'],
                   late_updates=(s1['late_count'] - s0['late_count']) & 0xFFFF,
                   blits_last=s1['blits'], blits_max=s1['blits_max'], rt_used_last=s1['rt_used'], rt_max=s1['rt_max'],
                   rt_full=(s1['rt_full'] - s0['rt_full']) & 0xFFFF,
                   rtmax=s1['rtmax'], err=s1['err'], objects_dropped=dropped, cells_no_ramtile=rtdrop,
                   cpu_lines_avg_excl_wait=round(((s1['cpu_sum'] - s0['cpu_sum']) - ((waited - w0) & 0xFFFF)) / max(nu, 1), 1))
        if trace:
            res['trace'] = trace
        print(json.dumps(res))
    finally:
        try:
            api.cmd('QUIT')
        except Exception:
            pass
        try:
            p.wait(5)
        except Exception:
            p.kill()


def to192(src, dst, phase=49, cpp=7):
    """cuzebox-ex's cycle capture (1440 cycles from cycle 299) -> the 192 Mode 748 pixels, 2x"""
    from PIL import Image
    import numpy as np
    a = np.array(Image.open(src).convert('RGB'))
    sub = a[:, phase:phase + cpp * 192:cpp]
    Image.fromarray(sub).resize((sub.shape[1] * 2, sub.shape[0] * 2), Image.NEAREST).save(dst)


if __name__ == '__main__':
    main()
