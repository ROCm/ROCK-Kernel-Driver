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

snd-page-alloc-objs := memalloc.o
ifeq ($(CONFIG_PCI),y)
snd-page-alloc-objs += sgbuf.o
endif

snd-rawmidi-objs  := rawmidi.o
snd-timer-objs    := timer.o
snd-rtctimer-objs := rtctimer.o
snd-hwdep-objs    := hwdep.o

obj-$(CONFIG_SND) += snd.o
ifeq ($(subst m,y,$(CONFIG_RTC)),y)
  obj-$(CONFIG_SND_RTCTIMER) += snd-timer.o
  obj-$(CONFIG_SND_RTCTIMER) += snd-rtctimer.o
endif
obj-$(CONFIG_SND_HWDEP) += snd-hwdep.o

obj-$(CONFIG_SND_MIXER_OSS) += oss/
obj-$(CONFIG_SND_PCM_OSS) += snd-pcm.o snd-timer.o snd-page-alloc.o oss/
obj-$(CONFIG_SND_SEQUENCER) += snd-timer.o seq/
obj-$(CONFIG_SND_BIT32_EMUL) += ioctl32/

# Toplevel Module Dependency
obj-$(CONFIG_SND_DUMMY) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_VIRMIDI) += snd-rawmidi.o snd.o snd-timer.o
obj-$(CONFIG_SND_SERIAL_U16550) += snd-rawmidi.o snd.o snd-timer.o
obj-$(CONFIG_SND_MTPAV) += snd-rawmidi.o snd.o snd-timer.o
obj-$(CONFIG_SND_MPU401) += snd-rawmidi.o snd.o snd-timer.o
obj-$(CONFIG_SND_ALS100) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_AZT2320) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_AZT3328) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_CMI8330) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_DT019X) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_ES18XX) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_OPL3SA2) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_SGALAXY) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_AD1816A) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_AD1848) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_CS4231) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_CS4232) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_CS4236) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_ES1688) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_GUSCLASSIC) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_GUSMAX) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_GUSEXTREME) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_INTERWAVE) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_INTERWAVE_STB) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_OPTI92X_AD1848) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_OPTI92X_CS4231) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_OPTI93X) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_SB8) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_SB16) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_SBAWE) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_ES968) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_WAVEFRONT) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_SSCAPE) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_ALS4000) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_CMIPCI) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_CS4281) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_ENS1370) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_ENS1371) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_ES1938) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-hwdep.o snd-rawmidi.o
obj-$(CONFIG_SND_ES1968) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_FM801) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_ICE1712) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_ICE1724) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_INTEL8X0) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_MAESTRO3) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_RME32) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_RME96) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_SONICVIBES) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_VIA82XX) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_ALI5451) += snd.o snd-rawmidi.o snd-timer.o snd-page-alloc.o snd-pcm.o
obj-$(CONFIG_SND_CS46XX) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_EMU10K1) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_KORG1212) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_NM256) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_RME9652) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_HDSP) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_TRIDENT) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_YMFPCI) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_POWERMAC) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_SA11XX_UDA1341) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_PC98_CS4232) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o snd-hwdep.o
ifeq ($(CONFIG_SND_SB16_CSP),y)
  obj-$(CONFIG_SND_SB16) += snd-hwdep.o
  obj-$(CONFIG_SND_SBAWE) += snd-hwdep.o
endif
obj-$(CONFIG_SND_USB_AUDIO) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_SUN_AMD7930) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_SUN_CS4231) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_HARMONY) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o
obj-$(CONFIG_SND_VXPOCKET) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-hwdep.o
obj-$(CONFIG_SND_VXP440) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-hwdep.o
obj-$(CONFIG_SND_VX222) += snd-pcm.o snd-timer.o snd-page-alloc.o snd.o snd-hwdep.o

obj-m := $(sort $(obj-m))
