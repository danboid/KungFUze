#!/usr/bin/env python3
"""Stage 2c: the game's sound effects, made from the user's own ROM set, as 2-bit ADPCM clips in SPI RAM.

Runs the sound ROMs (a-3h: the sound board's M6803 program; a-3e, a-3f: its ADPCM samples) on
STEArcade's model of the Kung-Fu Master sound board (third_party/stearcade/games/kungfum/tools/sound.py:
the M6803, two AY-3-8910s, two MSM5205s), sending it the arcade's own sound commands:

  kiai    $02  Thomas's shout on every punch and kick: the MSM5205 voice the command plays
  hurt    $03 + $11  Thomas hit: the hurt voice and the impact, which the arcade sends together
  impact  $11  a hit landing: an AY effect, rendered here from the board's timed AY writes
  throw   $15  a knife thrown: an AY effect
  death   $07  Thomas's death cry (sampled voice)
  step    $10  a footstep (the READY walk; an AY effect)
  laugh   $04  the boss's laugh, when he kills Thomas (sampled voice)
  coin    $01  the arcade's coin voice; here it greets Start (sampled voice)

(the commands as the game sends them in play, logged on the reference machine). Output: 15,734 Hz
(a sample a scanline), encoded with the bundled mixer's 2-bit ADPCM encoder
(third_party/kfa_mixer/tools/mixer_codecs.py) and placed in the unused end of SPI RAM bank 0.
Writes gen/kfm_audio.h. The clips are ROM-derived: never commit or distribute them.
"""
import argparse, re, struct, sys, wave
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
RATE = 28636360 / 1820          # the line rate: one sample a scanline
AY_CLOCK = 894886.25            # as the hand-off (3.579545 MHz / 4)
FIELD = 262
# AY-3-8910 output levels (MAME's measured table, normalised)
AY_LEVELS = [0.0, 0.0106, 0.0150, 0.0222, 0.0320, 0.0466, 0.0665, 0.0982,
             0.1394, 0.2111, 0.2920, 0.4075, 0.5258, 0.6920, 0.8291, 1.0]


def render_ay(events, total, max_s=0.8, over=8):
    """timed AY writes [(sample time, chip, register, value)] (two AY-3-8910s: 3 tones, noise and
    envelope each) -> mono samples at the line rate"""
    events = sorted(events, key=lambda e: e[0])
    regs = [[0] * 16 for _ in range(2)]
    for c in range(2):
        regs[c][7] = 0xFF
    tone_ph = [[0.0] * 3 for _ in range(2)]
    tone_out = [[0] * 3 for _ in range(2)]
    noise_lfsr = [1, 1]
    noise_ph = [0.0, 0.0]
    env_ph = [0.0, 0.0]
    env_step = [0, 0]
    env_hold = [False, False]
    dt = 1.0 / (RATE * over)
    ei = 0
    out = np.zeros(total * over)
    for s in range(total * over):
        t = s // over
        while ei < len(events) and events[ei][0] <= t:
            _, c, r, v = events[ei]
            r &= 15
            regs[c][r] = v
            if r == 13:
                env_step[c], env_ph[c], env_hold[c] = 0, 0.0, False
            ei += 1
        acc = 0.0
        for c in range(2):
            R = regs[c]
            # noise: shifts at clock / (16 * period)
            np_ = (R[6] & 31) or 1
            noise_ph[c] += dt * AY_CLOCK / (16 * np_)
            while noise_ph[c] >= 1.0:
                noise_ph[c] -= 1.0
                bit = (noise_lfsr[c] ^ (noise_lfsr[c] >> 3)) & 1
                noise_lfsr[c] = (noise_lfsr[c] >> 1) | (bit << 16)
            nout = noise_lfsr[c] & 1
            # envelope: 16 steps a cycle at clock / (256 * period)
            ep = (R[11] | (R[12] << 8)) or 1
            shape = R[13]
            if not env_hold[c]:
                env_ph[c] += dt * AY_CLOCK / (16 * ep)
                while env_ph[c] >= 1.0:
                    env_ph[c] -= 1.0
                    env_step[c] += 1
                    if env_step[c] >= 16 and not (shape & 8):
                        env_hold[c] = True
                        env_step[c] = 15
                    elif env_step[c] >= 16 and (shape & 1):   # hold
                        env_hold[c] = True
                        env_step[c] = 15
            k = env_step[c] % 32
            attack = bool(shape & 4)
            cyc = (env_step[c] // 16) & 1
            if (shape & 8) and (shape & 2) and cyc:
                attack = not attack                           # alternate
            lvl = (k % 16) if attack else 15 - (k % 16)
            if env_hold[c] and not (shape & 8):
                lvl = 0
            elif env_hold[c] and (shape & 1):
                lvl = (15 if attack else 0) ^ (15 if (shape & 2) else 0)
            for ch in range(3):
                per = (R[ch * 2] | ((R[ch * 2 + 1] & 15) << 8)) or 1
                tone_ph[c][ch] += dt * AY_CLOCK / (16 * per)
                if tone_ph[c][ch] >= 0.5:
                    tone_ph[c][ch] -= 0.5
                    tone_out[c][ch] ^= 1
                mix = R[7]
                tgate = tone_out[c][ch] | ((mix >> ch) & 1)
                ngate = nout | ((mix >> (ch + 3)) & 1)
                vol = R[8 + ch]
                level = AY_LEVELS[lvl] if vol & 16 else AY_LEVELS[vol & 15]
                acc += level * (tgate & ngate)
        out[s] = acc
    # decimate (box filter) and remove DC
    out = out.reshape(-1, over).mean(1)
    out -= np.median(out[: min(len(out), 2000)]) if len(out) else 0
    out -= out.mean()
    # end the effect once it has decayed below 10 % of its peak for 30 ms (some leave a channel
    # sounding quietly), with a 20 ms fade; and cap the length
    w = int(0.01 * RATE)
    lv = np.array([np.sqrt(np.mean(out[i:i + w] ** 2)) for i in range(0, len(out), w)]) if len(out) else np.zeros(1)
    pk = lv.max() or 1.0
    ipk = int(np.argmax(lv))
    end = len(out)
    for i in range(ipk, len(lv) - 2):
        if lv[i:i + 3].max() < 0.10 * pk:
            end = i * w
            break
    end = min(end, int(max_s * RATE))
    f = min(int(0.02 * RATE), end)
    if f:
        out[end - f:end] *= np.linspace(1.0, 0.0, f)
    return out[: min(end, int(max_s * RATE))]


def board(stearcade, rom, build):
    sys.path.insert(0, str(ROOT / 'tools'))
    import kfm_extract
    kfm_extract.setup(stearcade, rom, build)       # unpacks the ROMs (sound ones too) for the board
    import sound
    return sound


def booted(sound):
    """the board after its boot, as sound.voice_signal starts it (a stop command, then 30 frames)"""
    b = sound.Board()
    b.command_at(sound.FRAME_E * 1.7, 0)
    b.command_at(sound.FRAME_E * 1.7 + 5, 0x80)
    b.run(sound.FRAME_E * 30 - b.cpu.cycles)
    return b


def ay_clip(sound, cmd, max_s=0.8):
    """an AY effect: the board's AY writes for command `cmd` (with those of its boot first, at time 0)"""
    b = booted(sound)
    t0 = b.cpu.cycles
    b.command_at(t0 + 10, cmd)
    b.command_at(t0 + 15, cmd | 0x80)
    b.run((max_s + 0.2) * sound.E_HZ)
    ev = [(max(0.0, (c - t0) / sound.E_HZ * RATE), chip, reg, val) for c, chip, reg, val in b.ay_writes]
    return render_ay(ev, int((max_s + 0.1) * RATE), max_s)


def voice_clip(sound, cmd):
    """an MSM5205 voice: its 4 kHz output, resampled to the line rate (linear)"""
    s = np.array(sound.voice_signal(cmd), np.float64)
    t = np.arange(int(len(s) * RATE / sound.NMI_HZ)) * sound.NMI_HZ / RATE
    return np.interp(t, np.arange(len(s)), s)


FRAME_HZ = RATE / 262            # the Uzebox's frame rate (59.94 Hz)
MUSIC_END = 0x80                 # stream: the loop's end


def music_stream(sound, cmd=0x24, secs=35.0, loop=True):
    """the floor's music ($24): the board's AY writes for it, its loop found in the write sequence,
    and one loop as per-frame states of the two channels it uses (AY 45M: B a square-wave melody,
    A noise under the envelope: the drums), encoded as a change stream:
      per frame a flags byte (bit 0: channel B's 16-bit phase step follows, bit 1: its amplitude,
      bit 2: the drums' amplitude (channel A's noise), bit 3: channel A's tone step, bit 4: its
      amplitude), then those values; MUSIC_END ends the stream."""
    b = booted(sound)
    t0 = b.cpu.cycles
    n0 = len(b.ay_writes)
    b.command_at(t0 + 10, cmd)
    b.command_at(t0 + 15, cmd | 0x80)
    b.run(secs * sound.E_HZ)
    w = [((c - t0) / sound.E_HZ, r & 15, v) for c, ch, r, v in b.ay_writes[n0:] if ch == 0 and (r & 15) != 15]
    seq = [(r, v) for t, r, v in w]
    want_loop = loop
    loop = None
    for L in (range(200, len(seq) // 2) if want_loop else ()):   # the tune repeats from its start
        if seq[:len(seq) - L] == seq[L:]:
            loop = L
            break
    if loop is None and want_loop:
        raise SystemExit('the music\'s loop was not found in %.0f s' % secs)
    secs_loop = (w[loop][0] - w[0][0]) if want_loop else (w[-1][0] + 0.3 if w else 0.0)
    R = [0] * 16
    env_t = None
    i = 0
    states = []
    for f in range(int(round(secs_loop * FRAME_HZ))):
        t = f / FRAME_HZ
        while i < len(w) and w[i][0] <= t:
            _, r, v = w[i]
            R[r] = v
            if r == 13:
                env_t = w[i][0]
            i += 1
        per = R[2] | ((R[3] & 15) << 8)
        tv = 0 if R[9] & 16 else R[9] & 15
        step = int(round(232964.0 / per)) if per and not (R[7] & 2) and tv else 0   # (AY clock / 16 / line rate) * 65536 / period
        ep = (R[11] | (R[12] << 8)) or 1
        dl = 0 if env_t is None or (R[7] & 8) or not (R[8] & 16) else max(0, 15 - int((t - env_t) * AY_CLOCK / (16 * ep)))
        per2 = R[0] | ((R[1] & 15) << 8)                 # channel A as a tone (the jingles use A and B)
        tv2 = 0 if R[8] & 16 else R[8] & 15
        step2 = int(round(232964.0 / per2)) if per2 and not (R[7] & 1) and tv2 else 0
        states.append((min(step, 65535), int(round(AY_LEVELS[tv] * 30)) if step else 0, int(round(AY_LEVELS[dl] * 28)),
                       min(step2, 65535), int(round(AY_LEVELS[tv2] * 30)) if step2 else 0))
    out = bytearray()
    prev = (None,) * 5
    for st in states:
        fl = ((1 if st[0] != prev[0] else 0) | (2 if st[1] != prev[1] else 0) | (4 if st[2] != prev[2] else 0) |
              (8 if st[3] != prev[3] else 0) | (16 if st[4] != prev[4] else 0))
        out.append(fl)
        if fl & 1:
            out += struct.pack('<H', st[0])
        if fl & 2:
            out.append(st[1])
        if fl & 4:
            out.append(st[2])
        if fl & 8:
            out += struct.pack('<H', st[3])
        if fl & 16:
            out.append(st[4])
        prev = st
    out.append(MUSIC_END)
    return bytes(out), secs_loop, len(states)


def to_u8(x, peak=112.0):
    """centre on 128, normalise the peak (the hand-off's builder scales its clips to about +-112)"""
    m = np.max(np.abs(x)) or 1.0
    return np.clip(np.round(128 + x * (peak / m)), 0, 255).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True, help="the MAME set kungfum.zip")
    ap.add_argument('--stearcade', default=str(ROOT / 'third_party' / 'stearcade'))
    ap.add_argument('--mixers', default=str(ROOT / 'third_party' / 'kfa_mixer'),
                    help='the mixer (default: the bundled subset in third_party/kfa_mixer)')
    ap.add_argument('--build', default=str(ROOT / 'build'))
    ap.add_argument('--gen', default=str(ROOT / 'gen'))
    a = ap.parse_args()
    sys.path.insert(0, str(Path(a.mixers) / 'tools'))
    from mixer_codecs import encode, decode
    sound = board(a.stearcade, a.rom, Path(a.build))
    kiai, hurt_v = voice_clip(sound, 0x02), voice_clip(sound, 0x03)
    impact, throw = ay_clip(sound, 0x11), ay_clip(sound, 0x15)
    n = max(len(hurt_v), len(impact))
    hurt = np.zeros(n)                              # the arcade sends $03 and $11 together on damage
    hurt[:len(hurt_v)] += hurt_v / (np.max(np.abs(hurt_v)) or 1)
    hurt[:len(impact)] += 0.6 * impact / (np.max(np.abs(impact)) or 1)
    death = voice_clip(sound, 0x07)                 # Thomas's death cry (the arcade: $00, then $07)
    step = ay_clip(sound, 0x10, max_s=0.3)          # a footstep (the READY walk: $10 every 6 frames)
    clips = [('kiai', to_u8(kiai)), ('hurt', to_u8(hurt)), ('impact', to_u8(impact)), ('throw', to_u8(throw)),
             ('death', to_u8(death)), ('step', to_u8(step)),
             ('laugh', to_u8(voice_clip(sound, 0x04))),     # the boss's laugh when he kills Thomas
             ('coin', to_u8(voice_clip(sound, 0x01)))]      # the arcade's coin voice: here, Start
    build, gen = Path(a.build), Path(a.gen)
    spi = bytearray((build / 'KFMG.DAT').read_bytes())          # the game's data file
    at = (int((build / 'kfmg_free.txt').read_text(), 0) + 255) & ~255   # bank 0's free space
    out = ['/* Generated by tools/kfm_audio_build.py from the KungFuMasterAudio captures. */',
           '#ifndef KFM_AUDIO_H', '#define KFM_AUDIO_H',
           '/* 2-bit ADPCM clips: SPI RAM position, packed bytes, samples (15,734 Hz) */']
    for name, s in clips:
        enc = encode(bytes(s), 'adpcm2')
        dec = np.frombuffer(decode(enc, len(s), 'adpcm2'), np.uint8).astype(int)
        err = np.sqrt(np.mean((dec - s.astype(int)) ** 2))
        if at + len(enc) > 0x10000:
            raise SystemExit('the sound clips do not fit in SPI RAM bank 0')
        if any(spi[at:at + len(enc)]):
            raise SystemExit('SPI RAM bank 0 at 0x%04X is not free' % at)
        spi[at:at + len(enc)] = enc
        out.append('#define KFA_%s_POS 0x%05XUL' % (name.upper(), at))
        out.append('#define KFA_%s_BYTES %dUL' % (name.upper(), len(enc)))
        out.append('#define KFA_%s_SAMPLES %dUL' % (name.upper(), len(s)))
        print('%-7s %5d samples (%.2f s), %4d bytes at 0x%05X, rms error %.1f' %
              (name, len(s), len(s) / RATE, len(enc), at, err))
        at = (at + len(enc) + 15) & ~15
    jin, jin_s, jin_f = music_stream(sound, 0x20, secs=8.0, loop=False)     # the start jingle (once)
    if at + len(jin) > 0x10000 or any(spi[at:at + len(jin)]):
        raise SystemExit('the jingle does not fit in SPI RAM bank 0')
    spi[at:at + len(jin)] = jin
    out.append('#define KFA_JINGLE_POS 0x%05XUL   /* the start jingle ($20): plays once */' % at)
    print('jingle  %d frames (%.2f s), %d bytes at 0x%05X' % (jin_f, jin_s, len(jin), at))
    at = (at + len(jin) + 15) & ~15
    gov, gov_s, gov_f = music_stream(sound, 0x21, secs=8.0, loop=False)     # the game-over jingle (once)
    if at + len(gov) > 0x10000 or any(spi[at:at + len(gov)]):
        raise SystemExit('the game-over jingle does not fit in SPI RAM bank 0')
    spi[at:at + len(gov)] = gov
    out.append('#define KFA_GAMEOVER_POS 0x%05XUL   /* the game-over jingle ($21): plays once */' % at)
    out.append('#define KFA_GAMEOVER_FRAMES %dU' % gov_f)
    print('game over %d frames (%.2f s), %d bytes at 0x%05X' % (gov_f, gov_s, len(gov), at))
    at = (at + len(gov) + 15) & ~15
    mus, mus_s, mus_f = music_stream(sound)
    if at + len(mus) > 0x10000 or any(spi[at:at + len(mus)]):
        raise SystemExit('the music does not fit in SPI RAM bank 0')
    spi[at:at + len(mus)] = mus
    out.append('/* the floor\'s music ($24): a per-frame change stream (see music_stream), looping */')
    out.append('#define KFA_MUSIC_POS 0x%05XUL' % at)
    out.append('#define KFA_MUSIC_BYTES %dUL' % len(mus))
    out.append('#define KFA_MUSIC_END 0x%02XU' % MUSIC_END)
    print('music   %d frames (%.2f s loop), %d bytes at 0x%05X' % (mus_f, mus_s, len(mus), at))
    out.append('#endif')
    at += len(mus)
    (build / 'KFMG.DAT').write_bytes(bytes(spi))
    (build / 'kfmg_free.txt').write_text('0x%05X\n' % at)
    (gen / 'kfm_audio.h').write_text('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
