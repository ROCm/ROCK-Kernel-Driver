#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := _emu10k1.o

list-multi   := snd-emu10k1.o snd-emu10k1-synth.o

export-objs  := emu10k1_main.o

snd-emu10k1-objs := emu10k1.o emu10k1_main.o \
		    irq.o memory.o voice.o emumpu401.o emupcm.o io.o \
		    emuproc.o emumixer.o emufx.o
snd-emu10k1-synth-objs := emu10k1_synth.o emu10k1_callback.o emu10k1_patch.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_EMU10K1) += snd-emu10k1.o
ifeq ($(subst m,y,$(CONFIG_SND_SEQUENCER)),y)
  obj-$(CONFIG_SND_EMU10K1) += snd-emu10k1.o snd-emu10k1-synth.o
endif

include $(TOPDIR)/Rules.make

snd-emu10k1.o: $(snd-emu10k1-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-emu10k1-objs)

snd-emu10k1-synth.o: $(snd-emu10k1-synth-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-emu10k1-synth-objs)
