# KungFuMasterAudio mixer (bundled subset)

These files come from the KungFuMasterAudio hand-off by one of the Uzebox developers. They are
used in kfm748 with the author's permission, and are unchanged copies.

| File | What it is |
|---|---|
| `mixer.h`, `mixer_config.h`, `select.c` | The mixer's interface and profile selection |
| `variants/pcm_adpcm2_2v.c` | The profile kfm748 uses: two 2-bit ADPCM voices, no chip synthesis |
| `codecs/voice.h`, `codecs/adpcm2.c`, `codecs/adpcm2_table.inc` | The 2-bit ADPCM decoder |
| `cores/pcm.c` | The sample-only core |
| `tools/mixer_codecs.py`, `tools/megabomber_codec.py`, `tools/precedent_table.py` | The encoder, used by `tools/kfm_audio_build.py` |
| `README.md` | The catalog's guide |
| `reference/` | Its source notices |

Only this one profile is bundled. `select.c` opens only the selected variant, so the others are
not needed.

**kfm748's own adaptations**, outside this directory:
- `src/kfm_mixrt.c`: the hand-off's `runtime.c`, with the sample queue at a fixed SRAM page
  (0x0E00), and silence filled cheaply while both voices are idle.
- `src/kfm_sound.s`: a new per-line `update_sound` that plays the queue on the stock Uzebox inline
  mixer's cycle contract, so Mode 748 needs no change.

**Licence.** The 1/2-bit codec follows the Uzebox `dconvert` utility (Lee Weber, 2017;
`reference/dconvert_NOTICE.txt`), which is GPL version 3 or later. The catalog asks that the GPL
obligations of the copied Uzebox/dconvert/Tower code be preserved. This subset is therefore
distributed under the GNU General Public License, version 3 or later: see `COPYING.GPL3`, from the
Uzebox repository.

**Sounds.** kfm748 makes its sound clips from the user's own ROM set, on STEArcade's sound board
model (`tools/kfm_audio_build.py`). None of the hand-off's sound captures are used or included.
