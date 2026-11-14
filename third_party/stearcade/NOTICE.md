# STEArcade (bundled subset)

These files come from STEArcade by AnimaInCorpore, <https://github.com/AnimaInCorpore/STEArcade>,
revision 83108f9. They are unchanged copies.

**Licence.** The STEArcade code is free software under the GNU General Public License, version 2
or (at your option) any later version. See `LICENSE`, copied from the same revision. The licence
does not cover the arcade games themselves: their ROMs, and data converted from them.

**Use here.** kfm748's asset tools (`tools/kfm_extract.py`, `tools/kfm_capture.py`,
`tools/kfm_audio_build.py`) run the user's own Kung-Fu Master ROM set in STEArcade's reference
machine and sound board:

| File | What it is |
|---|---|
| `games/kungfum/tools/machine.py` | The Kung-Fu Master board model |
| `engine/z80.py`, `engine/z80mame.py`, `engine/z80mame_ops.py` | The Z80 CPU |
| `engine/mamegfx.py` | Graphics decoding |
| `engine/paths.py` | Where the ROM images are read from |
| `games/kungfum/tools/sound.py` | The Kung-Fu Master sound board: its M6803 program, the AYs and the MSM5205 ADPCM voices |
| `engine/m6801.py` | The M6801/M6803 CPU, for the sound board |

No ROM data is included here.
