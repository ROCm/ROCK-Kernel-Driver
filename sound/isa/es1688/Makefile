#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := _es1688.o

list-multi   := snd-es1688-lib.o snd-es1688.o

export-objs  := es1688_lib.o

snd-es1688-lib-objs := es1688_lib.o
snd-es1688-objs := es1688.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_ES1688) += snd-es1688.o snd-es1688-lib.o
obj-$(CONFIG_SND_GUSEXTREME) += snd-es1688-lib.o

include $(TOPDIR)/Rules.make

snd-es1688-lib.o: $(snd-es1688-lib-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-es1688-lib-objs)

snd-es1688.o: $(snd-es1688-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-es1688-objs)
