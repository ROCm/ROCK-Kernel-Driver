#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := _opl3.o

export-objs  := opl3_lib.o

snd-opl3-lib-objs := opl3_lib.o opl3_synth.o
snd-opl3-synth-objs := opl3_seq.o opl3_midi.o opl3_drums.o
ifeq ($(subst m,y,$(CONFIG_SND_SEQUENCER)),y)
snd-opl3-synth-objs += opl3_oss.o
endif

# Toplevel Module Dependency
obj-$(CONFIG_SND_ALS100) += snd-opl3-lib.o
obj-$(CONFIG_SND_AZT2320) += snd-opl3-lib.o
obj-$(CONFIG_SND_DT0197H) += snd-opl3-lib.o
obj-$(CONFIG_SND_ES18XX) += snd-opl3-lib.o
obj-$(CONFIG_SND_OPL3SA2) += snd-opl3-lib.o
obj-$(CONFIG_SND_AD1816A) += snd-opl3-lib.o
obj-$(CONFIG_SND_CS4232) += snd-opl3-lib.o
obj-$(CONFIG_SND_CS4236) += snd-opl3-lib.o
obj-$(CONFIG_SND_ES1688) += snd-opl3-lib.o
obj-$(CONFIG_SND_GUSEXTREME) += snd-opl3-lib.o
obj-$(CONFIG_SND_OPTI92X_AD1848) += snd-opl3-lib.o
obj-$(CONFIG_SND_OPTI92X_CS4231) += snd-opl3-lib.o
obj-$(CONFIG_SND_OPTI93X) += snd-opl3-lib.o
obj-$(CONFIG_SND_SB8) += snd-opl3-lib.o
obj-$(CONFIG_SND_SB16) += snd-opl3-lib.o
obj-$(CONFIG_SND_SBAWE) += snd-opl3-lib.o
obj-$(CONFIG_SND_WAVEFRONT) += snd-opl3-lib.o
obj-$(CONFIG_SND_ALS4000) += snd-opl3-lib.o
obj-$(CONFIG_SND_CMIPCI) += snd-opl3-lib.o
obj-$(CONFIG_SND_CS4281) += snd-opl3-lib.o
obj-$(CONFIG_SND_ES1938) += snd-opl3-lib.o
obj-$(CONFIG_SND_FM801) += snd-opl3-lib.o
obj-$(CONFIG_SND_SONICVIBES) += snd-opl3-lib.o
obj-$(CONFIG_SND_YMFPCI) += snd-opl3-lib.o
ifeq ($(subst m,y,$(CONFIG_SND_SEQUENCER)),y)
  obj-$(CONFIG_SND_ALS100) += snd-opl3-synth.o
  obj-$(CONFIG_SND_AZT2320) += snd-opl3-synth.o
  obj-$(CONFIG_SND_DT0197H) += snd-opl3-synth.o
  obj-$(CONFIG_SND_ES18XX) += snd-opl3-synth.o
  obj-$(CONFIG_SND_OPL3SA2) += snd-opl3-synth.o
  obj-$(CONFIG_SND_AD1816A) += snd-opl3-synth.o
  obj-$(CONFIG_SND_CS4232) += snd-opl3-synth.o
  obj-$(CONFIG_SND_CS4236) += snd-opl3-synth.o
  obj-$(CONFIG_SND_ES1688) += snd-opl3-synth.o
  obj-$(CONFIG_SND_GUSEXTREME) += snd-opl3-synth.o
  obj-$(CONFIG_SND_OPTI92X_AD1848) += snd-opl3-synth.o
  obj-$(CONFIG_SND_OPTI92X_CS4231) += snd-opl3-synth.o
  obj-$(CONFIG_SND_OPTI93X) += snd-opl3-synth.o
  obj-$(CONFIG_SND_SB8) += snd-opl3-synth.o
  obj-$(CONFIG_SND_SB16) += snd-opl3-synth.o
  obj-$(CONFIG_SND_SBAWE) += snd-opl3-synth.o
  obj-$(CONFIG_SND_WAVEFRONT) += snd-opl3-synth.o
  obj-$(CONFIG_SND_ALS4000) += snd-opl3-synth.o
  obj-$(CONFIG_SND_CMIPCI) += snd-opl3-synth.o
  obj-$(CONFIG_SND_CS4281) += snd-opl3-synth.o
  obj-$(CONFIG_SND_ES1938) += snd-opl3-synth.o
  obj-$(CONFIG_SND_FM801) += snd-opl3-synth.o
  obj-$(CONFIG_SND_SONICVIBES) += snd-opl3-synth.o
  obj-$(CONFIG_SND_YMFPCI) += snd-opl3-synth.o
endif

obj-m := $(sort $(obj-m))

include $(TOPDIR)/Rules.make
