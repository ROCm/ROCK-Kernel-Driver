#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := _ppc.o

snd-powermac-objs := powermac.o pmac.o awacs.o burgundy.o daca.o tumbler.o keywest.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_POWERMAC) += snd-powermac.o

include $(TOPDIR)/Rules.make
