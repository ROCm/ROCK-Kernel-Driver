#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

export-objs  := rme9652_mem.o

snd-rme9652-mem-objs := rme9652_mem.o
snd-rme9652-objs := rme9652.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_RME9652) += snd-rme9652.o snd-rme9652-mem.o

include $(TOPDIR)/Rules.make
