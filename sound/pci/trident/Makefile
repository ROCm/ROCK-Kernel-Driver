#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

export-objs  := trident_main.o

snd-trident-objs := trident.o trident_main.o trident_memory.o
snd-trident-synth-objs := trident_synth.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_TRIDENT) += snd-trident.o
ifeq ($(subst m,y,$(CONFIG_SND_SEQUENCER)),y)
  obj-$(CONFIG_SND_TRIDENT) += snd-trident-synth.o
endif

include $(TOPDIR)/Rules.make
