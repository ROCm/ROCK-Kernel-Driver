#
# Makefile for ALSA
# Copyright (c) 1999,2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-objs     := sound.o init.o memory.o info.o control.o misc.o \
                device.o wrappers.o
ifeq ($(CONFIG_ISA),y)
snd-objs     += isadma.o
endif
ifeq ($(CONFIG_SND_OSSEMUL),y)
snd-objs     += sound_oss.o info_oss.o
endif

snd-pcm-objs := pcm.o pcm_native.o pcm_lib.o pcm_timer.o pcm_misc.o \
		pcm_memory.o

snd-page-alloc-objs := memalloc.o sgbuf.o

snd-rawmidi-objs  := rawmidi.o
snd-timer-objs    := timer.o
snd-rtctimer-objs := rtctimer.o
snd-hwdep-objs    := hwdep.o

obj-$(CONFIG_SND) 		+= snd.o
obj-$(CONFIG_SND_HWDEP)		+= snd-hwdep.o
obj-$(CONFIG_SND_TIMER)		+= snd-timer.o
obj-$(CONFIG_SND_RTCTIMER)	+= snd-rtctimer.o
obj-$(CONFIG_SND_PCM)		+= snd-pcm.o snd-page-alloc.o
obj-$(CONFIG_SND_RAWMIDI)	+= snd-rawmidi.o

obj-$(CONFIG_SND_OSSEMUL)	+= oss/
obj-$(CONFIG_SND_SEQUENCER)	+= seq/
obj-$(CONFIG_SND_BIT32_EMUL) += ioctl32/

# Toplevel Module Dependency
obj-$(CONFIG_SND_VIRMIDI) += snd-timer.o
obj-$(CONFIG_SND_SERIAL_U16550) += snd-timer.o
obj-$(CONFIG_SND_MTPAV) += snd-timer.o
obj-$(CONFIG_SND_MPU401) += snd-timer.o
obj-$(CONFIG_SND_ALS100) += snd-hwdep.o
obj-$(CONFIG_SND_AZT2320) += snd-hwdep.o
obj-$(CONFIG_SND_AZT3328) += snd-hwdep.o
obj-$(CONFIG_SND_DT019X) += snd-hwdep.o
obj-$(CONFIG_SND_ES18XX) += snd-hwdep.o
obj-$(CONFIG_SND_OPL3SA2) += snd-hwdep.o
obj-$(CONFIG_SND_AD1816A) += snd-hwdep.o
obj-$(CONFIG_SND_CS4232) += snd-hwdep.o
obj-$(CONFIG_SND_CS4236) += snd-hwdep.o
obj-$(CONFIG_SND_ES1688) += snd-hwdep.o
obj-$(CONFIG_SND_GUSEXTREME) += snd-hwdep.o
obj-$(CONFIG_SND_OPTI92X_AD1848) += snd-hwdep.o
obj-$(CONFIG_SND_OPTI92X_CS4231) += snd-hwdep.o
obj-$(CONFIG_SND_OPTI93X) += snd-hwdep.o
obj-$(CONFIG_SND_SB8) += snd-hwdep.o
obj-$(CONFIG_SND_SB16) += snd-hwdep.o
obj-$(CONFIG_SND_SBAWE) += snd-hwdep.o
obj-$(CONFIG_SND_WAVEFRONT) += snd-hwdep.o
obj-$(CONFIG_SND_SSCAPE) += snd-hwdep.o
obj-$(CONFIG_SND_ALS4000) += snd-hwdep.o
obj-$(CONFIG_SND_CMIPCI) += snd-hwdep.o
obj-$(CONFIG_SND_CS4281) += snd-hwdep.o
obj-$(CONFIG_SND_ES1938) += snd-hwdep.o
obj-$(CONFIG_SND_FM801) += snd-hwdep.o
obj-$(CONFIG_SND_SONICVIBES) += snd-hwdep.o
obj-$(CONFIG_SND_EMU10K1) += snd-hwdep.o
obj-$(CONFIG_SND_HDSP) += snd-hwdep.o
obj-$(CONFIG_SND_YMFPCI) += snd-hwdep.o
obj-$(CONFIG_SND_PC98_CS4232) += snd-hwdep.o
obj-$(CONFIG_SND_VXPOCKET) += snd-hwdep.o
obj-$(CONFIG_SND_VXP440) += snd-hwdep.o
obj-$(CONFIG_SND_VX222) += snd-hwdep.o
obj-$(CONFIG_SND_MIXART) += snd-hwdep.o

obj-m := $(sort $(obj-m))
