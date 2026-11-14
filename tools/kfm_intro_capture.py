#!/usr/bin/env python3
"""Stage 1c: the arcade's intro, from the user's ROM set on STEArcade's reference machine.

  * the story (the attract sequence after power-on): its text as it types out, screen by screen,
    and every sprite of every frame (Thomas and Silvia attacked, Silvia carried off; Thomas
    finding the letter);
  * the letter screen after Start;
  * every (tile code, attribute) these screens use, for the intro tile set (kfm_build.py).

Output: build/kfm_intro.pkl (ROM-derived: never commit or distribute it).
"""
import argparse, pickle, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import kfm_extract
import kfm_capture

ROW0 = 5                # the intro shows arcade tile rows 5-27 (23 rows: the story's text to the copyright)
ROWS = 23
COL0 = 4                # and columns 4-28 (25: 24 shown, one for the scroll; the letter is centred)
STATIC_ROW = 20         # text from this row down is shown at once (the copyright), not typed
CREDIT = ['UZEBOX PORT BY', 'DAN MACDONALD 2026']   # under the copyright (moved up two rows)


def white_attr(intro):
    """the attribute of the story's white text (its most common)"""
    import collections
    return collections.Counter(at for sc in intro['screens'] for r, c, code, at in sc['text']).most_common(1)[0][0]


def tilemap(m):
    return [[(m.mem[0xD000 + r * 64 + 16 + c], m.mem[0xD800 + r * 64 + 16 + c]) for c in range(32)]
            for r in range(32)]


def text_cells(tm):
    """the typed text: (row, col, code, attr) of the visible characters above STATIC_ROW, reading order"""
    out = []
    for r in range(ROW0, STATIC_ROW):
        for c in range(32):
            code, at = tm[r][c]
            if 33 <= code < 127 and not (at & 0xC0):
                out.append((r, c, code, at))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stearcade', default=str(ROOT / 'third_party' / 'stearcade'))
    ap.add_argument('--rom', required=True)
    ap.add_argument('--build', default=str(ROOT / 'build'))
    a = ap.parse_args()
    build = Path(a.build)
    machine = kfm_extract.setup(a.stearcade, a.rom, build)
    H = machine.Video().height
    m = machine.KungFuPort()
    frames, screens, letter = [], [], None
    combos = set()
    COIN, START = 900, 960
    for f in range(1300):
        s = 0xFF
        if COIN <= f < COIN + 4:
            s &= ~0x08
        if START <= f < START + 4:
            s &= ~0x01
        m.system, m.p1 = s, 0xFF
        m.run_frame_port()
        st = m.mem[0xE000]
        tm = tilemap(m)
        if st == 5:                                       # the story
            key = text_cells(tm)
            if key:
                last = screens[-1]['text'] if screens else None
                n = min(len(key), len(last)) if last is not None else 0
                if last is None or key[:n] != last[:n]:          # not the same text growing: a new screen
                    screens.append(dict(static=None, text=[]))
                if len(key) >= len(screens[-1]['text']):
                    screens[-1]['text'] = key
                    screens[-1]['static'] = [row[:] for row in tm]
            objs = []
            for o in kfm_capture.objects(kfm_capture.entries(m.sprite_ram, H)):
                objs.append(tuple((e[1], e[2], e[3], e[4], e[5], e[6], e[7]) for e in o))   # absolute
            frames.append((max(0, len(screens) - 1), len(key), objs))
            for r in range(ROW0, ROW0 + ROWS):
                combos.update(tm[r][COL0 - 4:COL0 + 28])
        elif st == 0 and f > START + 20 and letter is None and f >= 1100:
            letter = [row[:] for row in tm]
            for r in range(32):
                combos.update(letter[r])
    for sc in screens:
        for r in range(32):
            combos.update(sc['static'][r])
    out = dict(frames=frames, screens=screens, letter=letter, combos=sorted(combos),
               row0=ROW0, rows=ROWS, col0=COL0, static_row=STATIC_ROW, letter_frames=1299 - START)
    (build / 'kfm_intro.pkl').write_bytes(pickle.dumps(out))
    print('story: %d frames, %d screens (%s characters); letter; %d tile combos' %
          (len(frames), len(screens), '/'.join(str(len(s['text'])) for s in screens), len(combos)))
    print('wrote', build / 'kfm_intro.pkl')


if __name__ == '__main__':
    main()
