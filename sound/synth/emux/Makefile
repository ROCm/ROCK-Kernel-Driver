#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

export-objs  := emux.o

snd-emux-synth-objs := emux.o emux_synth.o emux_seq.o emux_nrpn.o \
		       emux_effect.o emux_proc.o emux_oss.o soundfont.o

# Toplevel Module Dependency
ifeq ($(subst m,y,$(CONFIG_SND_SEQUENCER)),y)
  obj-$(CONFIG_SND_SBAWE) += snd-emux-synth.o
  obj-$(CONFIG_SND_EMU10K1) += snd-emux-synth.o
endif

include $(TOPDIR)/Rules.make
