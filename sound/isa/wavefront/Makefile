#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := _wavefront.o

#list-multi   := snd-wavefront-fx.o snd-wavefront-synth.o snd-wavefront.o
list-multi   := snd-wavefront.o

#export-objs  := wavefront_fx.o wavefront_synth.o

#snd-wavefront-fx-objs := wavefront_fx.o
#snd-wavefront-synth-objs := wavefront_synth.o wavefront_midi.o
#snd-wavefront-objs := wavefront.o
snd-wavefront-objs := wavefront.o wavefront_fx.o wavefront_synth.o wavefront_midi.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_WAVEFRONT) += snd-wavefront.o

include $(TOPDIR)/Rules.make

snd-wavefront-fx.o: $(snd-wavefront-fx-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-wavefront-fx-objs)

snd-wavefront-synth.o: $(snd-wavefront-synth-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-wavefront-synth-objs)

snd-wavefront.o: $(snd-wavefront-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-wavefront-objs)
