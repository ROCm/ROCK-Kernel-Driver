#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := _ali5451.o

list-multi   := snd-ali5451.o

snd-ali5451-objs := ali5451.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_ALI5451) += snd-ali5451.o

include $(TOPDIR)/Rules.make

snd-ali5451.o: $(snd-ali5451-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-ali5451-objs)
