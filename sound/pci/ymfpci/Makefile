#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := _ymfpci.o

list-multi   := snd-ymfpci.o

snd-ymfpci-objs := ymfpci.o ymfpci_main.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_YMFPCI) += snd-ymfpci.o

include $(TOPDIR)/Rules.make

snd-ymfpci.o: $(snd-ymfpci-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-ymfpci-objs)
