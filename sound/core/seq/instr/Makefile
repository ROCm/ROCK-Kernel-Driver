#
# Makefile for ALSA
# Copyright (c) 1999 by Jaroslav Kysela <perex@suse.cz>
#

snd-ainstr-fm-objs := ainstr_fm.o
snd-ainstr-simple-objs := ainstr_simple.o
snd-ainstr-gf1-objs := ainstr_gf1.o
snd-ainstr-iw-objs := ainstr_iw.o

#
# this function returns:
#   "m" - CONFIG_SND_SEQUENCER is m
#   <empty string> - CONFIG_SND_SEQUENCER is undefined
#   otherwise parameter #1 value
#
sequencer = $(if $(subst y,,$(CONFIG_SND_SEQUENCER)),$(if $(1),m),$(if $(CONFIG_SND_SEQUENCER),$(1)))

# Toplevel Module Dependency
obj-$(call sequencer,$(CONFIG_SND_ALS100)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_AZT2320)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_AZT3328)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_DT019X)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_ES18XX)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_OPL3SA2)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_AD1816A)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_CS4232)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_CS4236)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_ES1688)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_GUSCLASSIC)) += snd-ainstr-iw.o snd-ainstr-gf1.o snd-ainstr-simple.o
obj-$(call sequencer,$(CONFIG_SND_GUSMAX)) += snd-ainstr-iw.o snd-ainstr-gf1.o snd-ainstr-simple.o
obj-$(call sequencer,$(CONFIG_SND_GUSEXTREME)) += snd-ainstr-iw.o snd-ainstr-gf1.o snd-ainstr-simple.o snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_INTERWAVE)) += snd-ainstr-iw.o snd-ainstr-gf1.o snd-ainstr-simple.o
obj-$(call sequencer,$(CONFIG_SND_INTERWAVE_STB)) += snd-ainstr-iw.o snd-ainstr-gf1.o snd-ainstr-simple.o
obj-$(call sequencer,$(CONFIG_SND_OPTI92X_AD1848)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_OPTI92X_CS4231)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_OPTI93X)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_SB8)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_SB16)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_SBAWE)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_WAVEFRONT)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_ALS4000)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_CMIPCI)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_CS4281)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_ES1938)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_FM801)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_SONICVIBES)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_TRIDENT)) += snd-ainstr-simple.o
obj-$(call sequencer,$(CONFIG_SND_YMFPCI)) += snd-ainstr-fm.o

obj-m := $(sort $(obj-m))
