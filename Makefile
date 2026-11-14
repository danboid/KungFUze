ifeq ($(DEBUG),1)
EXTRA += -DKFM_DEBUG
endif
# Kung-Fu Master, Uzebox Mode 748
#
#   make assets ROM=/path/to/kungfum.zip       # graphics, moves and sound, all from the ROM set
#   make GAME=1 SOUND=1       # -> _bin_/kungfum.uze, the game with its data packed in
#   make                      # -> _bin_/kfm748.uze and _bin_/KFM.DAT: the sprite benchmark
#
# UZEBOX points at an Uzebox source tree (kernel/ and tools/packrom).

UZEBOX    ?= ../uzebox
STEARCADE ?= third_party/stearcade
MIXERS    ?= third_party/kfa_mixer
ROM       ?= kungfum.zip
ROM_BASE  ?= 0x8C00
LINES     ?= 216

TARGET = kfm748
MCU    = atmega644
CC     = avr-gcc
OUTDIR = _bin_
OBJDIR = _obj_
KERNEL_DIR = $(UZEBOX)/kernel
PACKROM    = $(UZEBOX)/tools/packrom/packrom

KERNEL_OPTIONS = -DVIDEO_MODE=748 -DINTRO_LOGO=0 -DSOUND_MIXER=MIXER_TYPE_INLINE
CFLAGS  = -mmcu=$(MCU) -Wall -gdwarf-2 -std=gnu99 -DF_CPU=28636360UL -Os -fsigned-char \
          -ffunction-sections -fdata-sections -fno-toplevel-reorder $(KERNEL_OPTIONS) \
          -I"$(KERNEL_DIR)" -Isrc -Igen -include m74cfg.h -DKFM_LINES=$(LINES)U $(GAME_FLAGS) $(SOUND_FLAGS) $(EXTRA)
ASMFLAGS = $(CFLAGS) -x assembler-with-cpp -Wa,-gdwarf2
LDFLAGS  = -mmcu=$(MCU) -Wl,-Map=$(OUTDIR)/$(TARGET).map -Wl,-gc-sections \
           -Wl,--section-start=.kfmrom=$(ROM_BASE)

DATFILE = $(OUTDIR)/KFM.DAT
PROPS   = gameinfo.properties
ifeq ($(GAME),1)
DATFILE    =
GAME_FLAGS = -DKFM_GAME
GAME_OBJ   = kfm_game.o kfm_intro.o
TARGET     = kungfum
endif

KOBJS = uzeboxVideoEngineCore.o uzeboxCore.o uzeboxSoundEngine.o uzeboxSoundEngineCore.o \
        uzeboxVideoEngine.o spiram.o bootlib.o
# SOUND=1 (with GAME=1): sound effects through the KungFuMasterAudio hand-off's foreground
# 2-voice 2-bit ADPCM mixer (bundled in third_party/kfa_mixer; MIXERS= to use another copy)
ifeq ($(SOUND),1)
SOUND_FLAGS = -DKFM_SOUND $(SOUND_FLAGS_C) -DKFM_QUEUE=$(QUEUE) -DMIXER_PCM_ADPCM2_2V=1 -I"$(MIXERS)"
SOUND_OBJ   = kfm_sound.o kfm_mixrt.o $(MIXVAR_OBJ) kfm_nokernsnd.o
ifneq ($(STOCK_ISR),1)
# (with kfm_sound.s the kernel's sound engine is not linked: src/kfm_nokernsnd.s stands in)
KOBJS      := $(filter-out uzeboxSoundEngine.o uzeboxSoundEngineCore.o,$(KOBJS))
endif
ifeq ($(STOCK_ISR),1)
# test build: the foreground sound (mixer, effects, SPI RAM reads) without kfm_sound.s: the kernel's
# own inline mixer stays the per-line routine (and plays nothing); for finding a hardware-only fault
SOUND_OBJ   = kfm_mixrt.o $(MIXVAR_OBJ)
endif
QUEUE      ?= 0x0E00
# the variant: with the per-sample render in assembly (kfm_mixvar.c + kfm_mix2.s), or C_MIXER=1:
# the hand-off's C through select.c (for comparison)
ifeq ($(C_MIXER),1)
MIXVAR_OBJ  = kfm_mixsel.o
SOUND_FLAGS_C = -DKFM_C_MIXER
else
MIXVAR_OBJ  = kfm_mixvar.o kfm_mix2.o
endif
SOUND_LD    = -Wl,--defsym=MixerSamples=$$((0x800000 + $(QUEUE)))
endif

OBJS  = $(addprefix $(OBJDIR)/,$(KOBJS) main.o kfb.o kfs.o kfm_rom.o kfm_tables.o $(GAME_OBJ) $(SOUND_OBJ))
GEN   = gen/kfm_rom.h gen/kfm_rom.c gen/kfm_tables.c

all: $(OUTDIR)/$(TARGET).uze $(DATFILE) size

assets:
	python3 tools/kfm_extract.py --stearcade "$(STEARCADE)" --rom "$(ROM)"
	python3 tools/kfm_capture.py --stearcade "$(STEARCADE)" --rom "$(ROM)"
	python3 tools/kfm_intro_capture.py --stearcade "$(STEARCADE)" --rom "$(ROM)"
	python3 tools/kfm_build.py --rom-base $(ROM_BASE)
	cd tools && python3 kfm_game_build.py
	python3 tools/kfm_audio_build.py --rom "$(ROM)" --stearcade "$(STEARCADE)" --mixers "$(MIXERS)"
	python3 tools/kfm_intro_build.py

$(GEN) build/KFM.DAT:
	@echo "Generated assets missing: run 'make assets ROM=...' first"; false

$(OBJDIR) $(OUTDIR):
	mkdir -p $@

# NOSOFTRESET=1: the kernel's controller code without its Start+Select+Y+B restart into the
# bootloader (a phantom reading on a controller port can trigger it)
ifeq ($(NOSOFTRESET),1)
$(OBJDIR)/uzeboxCore.o: $(KERNEL_DIR)/uzeboxCore.c $(GEN) | $(OBJDIR)
	sed 's/^\([[:space:]]*\)SoftReset();/\1\/* SoftReset() removed: NOSOFTRESET=1 *\//' $< > $(OBJDIR)/uzeboxCore_nsr.c
	grep -q 'NOSOFTRESET=1' $(OBJDIR)/uzeboxCore_nsr.c
	$(CC) $(CFLAGS) -c $(OBJDIR)/uzeboxCore_nsr.c -o $@
endif

ifeq ($(SOUND)$(STOCK_ISR),1)
# the stock inline mixer stays (its channels silent); only its update_sound is renamed,
# so src/kfm_sound.s plays the queue on the same cycle contract
$(OBJDIR)/uzeboxSoundEngineCore.o: $(KERNEL_DIR)/uzeboxSoundEngineCore.s $(GEN) | $(OBJDIR)
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@.tmp
	avr-objcopy --redefine-sym update_sound=update_sound_stock $@.tmp $@
	rm -f $@.tmp
endif
ifeq ($(SOUND),1)
$(OBJDIR)/kfm_mixsel.o: $(MIXERS)/select.c | $(OBJDIR)
	$(CC) $(CFLAGS) -O2 -c $< -o $@
endif

$(OBJDIR)/%.o: $(KERNEL_DIR)/%.s $(GEN) | $(OBJDIR)
	$(CC) $(ASMFLAGS) -c $< -o $@
$(OBJDIR)/%.o: $(KERNEL_DIR)/%.c $(GEN) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/main.o: src/main.c src/game.c src/m74cfg.h $(GEN) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/kfb.o: src/kfb.s | $(OBJDIR)
	$(CC) $(ASMFLAGS) -c $< -o $@

$(OBJDIR)/kfs.o: src/kfs.s | $(OBJDIR)
	$(CC) $(ASMFLAGS) -c $< -o $@

$(OBJDIR)/kfm_sound.o: src/kfm_sound.s | $(OBJDIR)
	$(CC) $(ASMFLAGS) -c $< -o $@

$(OBJDIR)/kfm_mixvar.o: src/kfm_mixvar.c | $(OBJDIR)
	$(CC) $(CFLAGS) -O2 -c $< -o $@

$(OBJDIR)/kfm_nokernsnd.o: src/kfm_nokernsnd.s | $(OBJDIR)
	$(CC) $(ASMFLAGS) -c $< -o $@

$(OBJDIR)/kfm_mix2.o: src/kfm_mix2.s | $(OBJDIR)
	$(CC) $(ASMFLAGS) -c $< -o $@

$(OBJDIR)/kfm_mixrt.o: src/kfm_mixrt.c | $(OBJDIR)
	$(CC) $(CFLAGS) -O2 -c $< -o $@

$(OBJDIR)/%.o: gen/%.c $(GEN) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/$(TARGET).elf: $(OBJS) | $(OUTDIR)
	$(CC) $(LDFLAGS) $(SOUND_LD) $(OBJS) -o $@

$(OUTDIR)/$(TARGET).hex: $(OUTDIR)/$(TARGET).elf
	avr-objcopy -O ihex -R .eeprom $< $@

ifeq ($(GAME),1)
# The game's data goes inside the .uze, as in IK748 and stuntcar-uzebox: packrom's header and
# program, padded with zeros to a whole 512-byte sector, then the data. The bootloader flashes only
# the program (its size is in the header); the game opens its own KUNGFUM.UZE, skips the header and
# program to the next whole sector, and loads the data from there. kfm_pack.py stamps the data's
# length and CRC-16 into it, which the game checks as it loads.
$(OBJDIR)/KFMG.DAT: build/KFMG.DAT tools/kfm_pack.py | $(OBJDIR)
	python3 tools/kfm_pack.py $< $@

$(OUTDIR)/$(TARGET).uze: $(OUTDIR)/$(TARGET).hex $(OBJDIR)/KFMG.DAT
	$(PACKROM) $< $@ gameinfo.properties
	n=$$(wc -c < $@); dd if=/dev/zero bs=1 count=$$(( (512 - n % 512) % 512 )) 2>/dev/null >> $@
	cat $(OBJDIR)/KFMG.DAT >> $@
	@echo "$@: $$(wc -c < $@) bytes (the program, then its data from sector $$(( ($$(wc -c < $@) - $$(wc -c < $(OBJDIR)/KFMG.DAT)) / 512 )))"
else
$(OUTDIR)/$(TARGET).uze: $(OUTDIR)/$(TARGET).hex
	$(PACKROM) $< $@ $(PROPS)

$(OUTDIR)/KFM.DAT: build/KFM.DAT | $(OUTDIR)
	cp $< $@
endif

size: $(OUTDIR)/$(TARGET).elf
	@avr-size -A $< | grep -E "^\.(text|data|bss|kfmrom)" ; \
	 echo "RAM: .data+.bss end at 0x$$(avr-nm $< | grep ' __bss_end' | cut -c5-8)"

clean:
	rm -rf $(OBJDIR) $(OUTDIR)

.PHONY: all assets size clean
