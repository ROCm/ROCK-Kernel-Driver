#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-opl3-lib-objs := opl3_lib.o opl3_synth.o
snd-opl3-synth-objs := opl3_seq.o opl3_midi.o opl3_drums.o
ifeq ($(CONFIG_SND_SEQUENCER_OSS),y)
snd-opl3-synth-objs += opl3_oss.o
endif

OPL3_OBJS = snd-opl3-lib.o
ifeq ($(subst m,y,$(CONFIG_SND_SEQUENCER)),y)
OPL3_OBJS += snd-opl3-synth.o
endif

# Toplevel Module Dependency
obj-$(CONFIG_SND_ALS100) += $(OPL3_OBJS)
obj-$(CONFIG_SND_AZT2320) += $(OPL3_OBJS)
obj-$(CONFIG_SND_AZT3328) += $(OPL3_OBJS)
obj-$(CONFIG_SND_DT019X) += $(OPL3_OBJS)
obj-$(CONFIG_SND_ES18XX) += $(OPL3_OBJS)
obj-$(CONFIG_SND_OPL3SA2) += $(OPL3_OBJS)
obj-$(CONFIG_SND_AD1816A) += $(OPL3_OBJS)
obj-$(CONFIG_SND_CS4232) += $(OPL3_OBJS)
obj-$(CONFIG_SND_PC98_CS4232) += $(OPL3_OBJS)
obj-$(CONFIG_SND_CS4236) += $(OPL3_OBJS)
obj-$(CONFIG_SND_ES1688) += $(OPL3_OBJS)
obj-$(CONFIG_SND_GUSEXTREME) += $(OPL3_OBJS)
obj-$(CONFIG_SND_OPTI92X_AD1848) += $(OPL3_OBJS)
obj-$(CONFIG_SND_OPTI92X_CS4231) += $(OPL3_OBJS)
obj-$(CONFIG_SND_OPTI93X) += $(OPL3_OBJS)
obj-$(CONFIG_SND_SB8) += $(OPL3_OBJS)
obj-$(CONFIG_SND_SB16) += $(OPL3_OBJS)
obj-$(CONFIG_SND_SBAWE) += $(OPL3_OBJS)
obj-$(CONFIG_SND_WAVEFRONT) += $(OPL3_OBJS)
obj-$(CONFIG_SND_ALS4000) += $(OPL3_OBJS)
obj-$(CONFIG_SND_CMIPCI) += $(OPL3_OBJS)
obj-$(CONFIG_SND_CS4281) += $(OPL3_OBJS)
obj-$(CONFIG_SND_ES1938) += $(OPL3_OBJS)
obj-$(CONFIG_SND_FM801) += $(OPL3_OBJS)
obj-$(CONFIG_SND_SONICVIBES) += $(OPL3_OBJS)
obj-$(CONFIG_SND_YMFPCI) += $(OPL3_OBJS)

obj-m := $(sort $(obj-m))
