#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

export-objs  := ice1712.o

snd-ice1712-objs := ice1712.o ak4524.o delta.o hoontech.o ews.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_ICE1712) += snd-ice1712.o

include $(TOPDIR)/Rules.make
