#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-opl4-lib-objs := opl4_lib.o opl4_mixer.o opl4_proc.o
snd-opl4-synth-objs := opl4_seq.o opl4_synth.o yrw801.o

OPL4_OBJS := snd-opl4-lib.o
ifeq ($(subst m,y,$(CONFIG_SND_SEQUENCER)),y)
  OPL4_OBJS += snd-opl4-synth.o
endif

obj-$(CONFIG_SND_OPTI92X_AD1848) += $(OPL4_OBJS)
obj-$(CONFIG_SND_OPTI92X_CS4231) += $(OPL4_OBJS)
