#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := _wavefront.o

list-multi   := snd-wavefront.o

snd-wavefront-objs := wavefront.o wavefront_fx.o wavefront_synth.o wavefront_midi.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_WAVEFRONT) += snd-wavefront.o

include $(TOPDIR)/Rules.make

snd-wavefront.o: $(snd-wavefront-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-wavefront-objs)
