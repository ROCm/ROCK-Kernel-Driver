#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := synth.o

subdir-y     := emux
subdir-m     := $(subdir-y)

list-multi   := snd-util-mem.o

export-objs  := util_mem.o

snd-util-mem-objs := util_mem.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_TRIDENT) += snd-util-mem.o
obj-$(CONFIG_SND_EMU10K1) += snd-util-mem.o
ifeq ($(subst m,y,$(CONFIG_SND_SEQUENCER)),y)
  obj-$(CONFIG_SND_SBAWE) += snd-util-mem.o
endif

include $(TOPDIR)/Rules.make

snd-util-mem.o: $(snd-util-mem-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-util-mem-objs)
