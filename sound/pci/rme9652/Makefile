#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-hammerfall-mem-objs := hammerfall_mem.o
snd-rme9652-objs := rme9652.o
snd-hdsp-objs := hdsp.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_RME9652) += snd-rme9652.o snd-hammerfall-mem.o
obj-$(CONFIG_SND_HDSP) += snd-hdsp.o snd-hammerfall-mem.o
