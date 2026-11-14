# Selectable Uzebox mixers

AI assistants: start with `../AGENTS.md` for project context and handoff rules.

Choose one of 90 ready-made implementations. The existing Kung Fu Master
sound test remains in `../default/KFMAUDIO.uze`; this collection is separate.
Each test ROM is a single padded `MIXTEST.uze` with its assets concatenated
using packrom `dependency=`. Copy only that selected UZE to SD, retaining
its filename. No runtime data files are needed.

| Family | Synthesized channels | Sample voices |
| --- | --- | --- |
| `pcm` | None | One or two independent voices |
| `ay` | Three shared tones, one shared noise generator, AY envelopes and effect borrowing | One or two independent voices |
| `apu` | Native 2A03 pulse1, pulse2, triangle, noise, envelopes, length/sweep units | One or two independent voices |

For each family there are five one-voice choices and 25 ordered two-voice
pairs. Two voices may use different codecs, including4-bit music with 2-bit
SFX. Only the selected translation unit and its codecs are compiled.

## Choose and build

Using GNU make 4.3+, Python 3 and the AVR toolchain, from this directory:

```sh
make list
make MIXER=ay_adpcm4_1v
make MIXER=ay_adpcm4_adpcm2_2v
make MIXER=apu_adpcm2_adpcm1_2v
make MIXER=pcm_adpcm3_2v
```

Output: `build/<profile>/MIXTEST.uze`. The two depths in a mixed profile name
refer to voice0 and voice1 respectively. Equal-depth pairs shorten the name
as in `ay_adpcm4_2v`. `apu` means 2A03; no 6502 interpreter is used.

For direct integration, compile `select.c` and `runtime.c` with exactly one
flag such as `-DMIXER_AY_ADPCM4_ADPCM2_2V=1`, and select `queue_inline.s` as
the kernel custom mixer. `mixer_config.h` rejects zero or multiple selections.
The kernel contract is 217 normal HSYNC cycles and 261 VSYNC cycles, with
SYNC edges at 128 and 61 cycles respectively. `mixer.h` is the public API.

`variants/` contains the explicit cases. They include the needed decoder(s)
and chip core, then spell out one or two voice calls. There is no runtime
codec switch, function-pointer dispatch per sample, or nested configuration
maze. `tools/generate.py` maintains the duplicated variants and transition
tables; edit that generator before regenerating these files.

## Codec formats

| Name | Bits/sample | Format and compatibility |
| --- | --- | --- |
| `adpcm1` | 1 | Original TowerOfRubble/dconvert transform 5 precedent scaling; arithmetic decoder preserves its counters and slope behavior |
| `adpcm2` | 2 | Original TowerOfRubble/dconvert transform 6; exact 90-state minimized lookup table, as in MegaBomber/SFA2 |
| `adpcm3` | 3 | Explicit three-decision precedent extrapolation, 54-state minimized table; continuous packed bits crossing bytes |
| `adpcm4` | 4 | MegaBomber's 16-state fast adaptive codec, 512-byte transition table |
| `fixed4` | 4 | The Kung Fu Master test's fixed delta nibble format; this is delta PCM, not adaptive ADPCM |

The adaptive1/2/3/4-bit streams pack symbols and bits least-significant first.
`fixed4` packs the high nibble first, with delta=nibble-8; nibble0 retains its
legacy stop meaning. All profiles also use exact sample and byte lengths, so
padding cannot become extra samples. The fixed4 test fixture emits an initial
zero delta to include its initial level in that explicit sample count.

1-bit starts at accumulator 128, slope 2 and cleared up/down/neutral counters.
After six samples without a run event, the slope decays by one if nonzero.
A run of three matching directions increases the slope by two when it is
below 16. The source permits 15 to become 17; that behavior is deliberately
preserved. Its exact minimized machine exceeds an8-bit state index, so this
case keeps the original small arithmetic implementation instead of silently
changing the codec to fit a table.

3-bit starts at accumulator 128 and slope 4. Its decisions have weights
slope, slope/2, slope/4, encoded as bits 2,1,0. Neutral count 3 decays the slope
toward 3; a direction run of at least 3 raises it by one toward 20. Counters
reset before the run check when neutral decay fires, following the existing
precedent family. Accumulation wraps modulo 256. This is a documented new
format. It is neither IMA/MSM5205 ADPCM nor dconvert's separate experimental
transform 8, which uses a different two-tap predictor and packing scheme.

The Python encoders and reference decoders are in `tools/mixer_codecs.py`.
Re-encode assets when changing codec selection; renaming a4-bit file to a
1/2/3-bit asset does not convert it. Each test ROM's assets are generated for
its exact pair automatically. The original accepted voice and baked hit are
the common source fixtures; this does not require the original arcade ROMs.

## Timing, SPI and integration

These catalog variants decode ADPCM and synthesize chip audio in foreground
C into a 256-byte queue. HSYNC consumes a complete sample on every line,
including VSYNC. SPI RAM retains the packed assets; they are not expanded
wholesale into AVR SRAM. Each voice has a 32-byte encoded-data cache. The
queue is limited to 240 samples, about 15.3ms at the NTSC line rate. Schedule
register writes by rendering up to the desired sample, then writing registers
before rendering subsequent samples.

The catalog test ROM defaults to a 240×128 picture. This provides CPU time for
the full simultaneous three-tone/noise/envelope and two-voice stress tests.
The tested full AY dual-voice workload underruns with a 176-line picture;
there is no claim that every variant/workload fits every video mode. Qualify
foreground time in the destination game. `DEMO_LINES=144`,160 or176 builds a
taller test picture for that measurement, for example:

```sh
make MIXER=ay_adpcm4_adpcm2_2v DEMO_LINES=176
python3 tools/check_avr.py --emulator /path/to/cuzebox --profile ay_adpcm4_adpcm2_2v
```

The checker rejects queue underruns and skipped audio fields. A failed taller
trial does not replace the default qualification: rebuild the default128-line
ROM and rerun its checks before distributing that build. The original
Kung Fu Master sound test still uses its approved176-line display and its
existing direct-HSYNC fixed4 PCM decoder.

Because the catalog's IRQ does not touch SPI, its demo storage callback may
read encoded caches across HSYNC; it only splits at64KiB bank boundaries.
Other games whose renderer/IRQ also owns SPI must supply their own safe
`MixerReadFn` and account for its wait time. The original production mixer
still needs its narrow SPI ownership windows. The test fixture deliberately
starts a clip at offset 65520, forcing a real cross-bank read.

`MixerPlay(voice,&clip,gain)` takes an absolute SPI byte offset, packed byte
count, exact sample count and initial predictor state. Fresh streams use
accumulator 128/state 0. For1-bit streams, state0 resets the arithmetic
counters and slope; it is not an arbitrary in-stream checkpoint. In mixed-depth builds each voice must receive its own
selected codec. Gain 128 is unity. Gain 28 for a single voice, or 14 per voice
for a pair, leaves room for chip audio. The final sum saturates to0..255.
`MixerStopVoice` stops just that voice; `MixerStop` stops chip and sample
sources. Already queued audio drains before silence, within the queue lead.
Do not call these foreground APIs from an ISR.

`MixerRender` retains its blocking behavior. `MixerRenderAvailable(maxSamples)`
returns the exact number generated and yields when the queue is full, letting
other foreground jobs run. Keep track of that count when scheduling register
events. This avoids queue-space polling; your SPI callback may still wait.
The optimized catalog chip state is private to foreground code, so it uses
neither volatile publications nor interrupt locks. Keep IRQ-shared queue
indices/data volatile; do not apply this change to the production SPI mixer.
See `OPTIMIZATION.md` for measured changes and retained limitations.

AY retains the shared noise/voice borrowing limits of the approved standalone
core; it is not six independent tone oscillators. The native2A03 core retains
the existing scanline-rate noise approximation, including the fastest noise
periods collapsing to one shift per line. Its normal frame sequencer clocks
four quarter and two half units per 262 samples. CPU IRQs, five-step frame
sequencing and NES DMC DMA are outside this subset; external ADPCM supplies
the sample voices. These are native audio subsets, not cycle-exact complete
chip emulators.

## Size/speed choices

The default mixer C compilation uses `MIXER_OPT=-O2`. `MIXER_OPT=-Os` is a
smaller-code option; timing must be requalified. Command-line changes rebuild
the selected output automatically, without `make clean`.

AY uses an exact 8KiB tone-step table by default. Set
`EXTRA_FLAGS=-DAY_TONE_STEP_TABLE=0` to use the smaller fixed-numerator divider.
The matching C equation and existing smaller envelope/length loop options
are documented beside the code. `EXTRA_FLAGS=-DAY_NOISE_ENABLE=0` omits AY
noise while retaining the tone/sample architecture. These tuning flags do
not select another mixer and do not permit multiple profile flags.

`CodecScale` has a short AVR multiply/shift implementation and an equivalent
portable C branch beside it.1-bit counters are arithmetic;2/3/4-bit transitions
are explicit, small tables. All depths have their own unpacking implementation
rather than a runtime-variable bit reader.3-bit packing uses a16-bit reservoir
so samples may straddle bytes without wasting two bits per byte.

## Reproduce validation

```sh
make matrix
python3 tools/check_formats.py
python3 tools/check_host.py
python3 tools/check_avr.py --emulator /path/to/cuzebox
```

Host checks execute each selected C implementation against the reference
PCM decoder, including exact stop lengths, gain, independent mixed codecs,
restarts,3-bit packing and cross-bank callback addresses. Invalid compile
selections must fail. The original Tower1/2-bit decoder is retained under
`reference/` as an independent compatibility reference.

AVR checks boot every padded UZE, validate its concatenated payload/CRC, play
simultaneous voices and chip audio, reject underruns/skipped fields/clipping,
verify silence after stop, and check the linked IRQ edges and cycle totals.
No SD accesses are permitted during playback. Actual AVR audio is saved as
`build/<profile>/preview.wav`; reports and measured memory usage accompany
the builds. These checks do not establish perceptual or exact analog-chip
accuracy. Hardware playback is not yet verified.

The original direct-HSYNC AY/fixed4 implementation is in `../audio/`; the
previous direct-HSYNC2A03/fixed4 copy is in `../default/2a03-before/`. They
retain their existing ABI, SPI rules and timing checks; they are not silently
replaced by this foreground catalog. MegaBomber/SFA2's dual direct-HSYNC
schedulers use a different224-cycle contract and their own SPI/UART ownership;
their assembly is a source reference, not an interchangeable217-cycle drop-in.

Source paths and hashes are recorded in `reference/sources.json`. Copied
Uzebox/dconvert/Tower code retains its original GPL obligations; preserve
those notices and the parent project's attribution files when handing off.

The demo uses the local `../video/` mode1 raster correction: normal SYNC
falling edges are exactly1820 clocks apart, including renderer entry/exit.
Do not mask interrupts to protect mixer/storage state; observe a fresh HSYNC
before any transaction that must fit its1440-cycle foreground window.
Catalog IRQs do not use SPI; games with different SPI ownership still need
a safe storage callback. The historical optimization comparison predates
this raster correction and retains the measured binaries' hashes.
