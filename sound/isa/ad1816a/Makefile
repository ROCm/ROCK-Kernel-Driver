#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := _ad1816a.o

list-multi   := snd-ad1816a-lib.o snd-ad1816a.o

export-objs  := ad1816a_lib.o

snd-ad1816a-lib-objs := ad1816a_lib.o
snd-ad1816a-objs := ad1816a.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_AD1816A) += snd-ad1816a.o snd-ad1816a-lib.o

include $(TOPDIR)/Rules.make

snd-ad1816a-lib.o: $(snd-ad1816a-lib-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-ad1816a-lib-objs)

snd-ad1816a.o: $(snd-ad1816a-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-ad1816a-objs)
