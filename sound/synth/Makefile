#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-util-mem-objs := util_mem.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_EMU10K1) += snd-util-mem.o
obj-$(CONFIG_SND_TRIDENT) += snd-util-mem.o
ifdef CONFIG_SND_SEQUENCER
  obj-$(CONFIG_SND_SBAWE) += snd-util-mem.o
  obj-$(CONFIG_SND) += emux/
endif
