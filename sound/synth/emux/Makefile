#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

export-objs  := emux.o

snd-emux-synth-y := emux.o emux_synth.o emux_seq.o emux_nrpn.o \
		    emux_effect.o emux_proc.o soundfont.o
snd-emux-synth-$(CONFIG_SND_SEQUENCER_OSS) += emux_oss.o

obj-$(CONFIG_SND_SBAWE) += snd-emux-synth.o
obj-$(CONFIG_SND_EMU10K1) += snd-emux-synth.o

include $(TOPDIR)/Rules.make
