#
# Makefile for ALSA
# Copyright (c) 1999,2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := core.o

export-objs  := sound.o pcm.o pcm_lib.o rawmidi.o timer.o rtctimer.o hwdep.o

list-multi   := snd.o snd-pcm.o snd-rawmidi.o snd-timer.o snd-rtctimer.o snd-hwdep.o

snd-objs     := sound.o init.o isadma.o memory.o info.o control.o misc.o \
                device.o wrappers.o
ifeq ($(CONFIG_SND_OSSEMUL),y)
snd-objs     += sound_oss.o info_oss.o
endif

snd-pcm-objs := pcm.o pcm_native.o pcm_lib.o pcm_timer.o pcm_misc.o \
		pcm_memory.o

snd-rawmidi-objs  := rawmidi.o
snd-timer-objs    := timer.o
snd-rtctimer-objs := rtctimer.o
snd-hwdep-objs    := hwdep.o

obj-$(CONFIG_SND) += snd.o
ifeq ($(CONFIG_RTC),y)
  obj-$(CONFIG_SND_RTCTIMER) += snd-timer.o
  obj-$(CONFIG_SND_RTCTIMER) += snd-rtctimer.o
endif
obj-$(CONFIG_SND_HWDEP) += snd-hwdep.o

subdir-$(CONFIG_SND_MIXER_OSS) += oss
subdir-$(CONFIG_SND_PCM_OSS) += oss
ifeq ($(filter $(subdir-y),oss),oss)
  subdir-m += oss
  obj-y += oss/oss.o
endif

subdir-$(CONFIG_SND_SEQUENCER) += seq
ifeq ($(CONFIG_SND_SEQUENCER),y)
  subdir-m += seq
  obj-y += seq/sq.o
endif

obj-$(CONFIG_SND_SEQUENCER) += snd-timer.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_DUMMY) += snd-pcm.o snd-timer.o snd.o
obj-$(CONFIG_SND_VIRMIDI) += snd-rawmidi.o snd.o snd-timer.o
obj-$(CONFIG_SND_MTPAV) += snd-rawmidi.o snd.o snd-timer.o
obj-$(CONFIG_SND_MPU401) += snd-rawmidi.o snd.o snd-timer.o
obj-$(CONFIG_SND_ALS100) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_AZT2320) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_CMI8330) += snd-pcm.o snd-timer.o snd.o
obj-$(CONFIG_SND_DT0197H) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_ES18XX) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_OPL3SA2) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_SGALAXY) += snd-pcm.o snd-timer.o snd.o
obj-$(CONFIG_SND_AD1816A) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_AD1848) += snd-pcm.o snd-timer.o snd.o
obj-$(CONFIG_SND_CS4231) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_CS4232) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_CS4236) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_ES1688) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_GUSCLASSIC) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_GUSMAX) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_GUSEXTREME) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_INTERWAVE) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_INTERWAVE_STB) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_OPTI92X_AD1848) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_OPTI92X_CS4231) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_OPTI93X) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_SB8) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_SB16) += snd-pcm.o snd-timer.o snd.o snd-hwdep.o snd-rawmidi.o
obj-$(CONFIG_SND_SBAWE) += snd-pcm.o snd-timer.o snd.o snd-hwdep.o snd-rawmidi.o
obj-$(CONFIG_SND_ES968) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_WAVEFRONT) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_ALS4000) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_CMIPCI) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_CS4281) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_ENS1370) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_ENS1371) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_ES1938) += snd-pcm.o snd-timer.o snd.o snd-hwdep.o snd-rawmidi.o
obj-$(CONFIG_SND_ES1968) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_FM801) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_ICE1712) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_INTEL8X0) += snd-pcm.o snd-timer.o snd.o
obj-$(CONFIG_SND_MAESTRO3) += snd-pcm.o snd-timer.o snd.o
obj-$(CONFIG_SND_RME96) += snd-pcm.o snd-timer.o snd.o
obj-$(CONFIG_SND_SONICVIBES) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_VIA686) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_VIA8233) += snd-pcm.o snd-timer.o snd.o
obj-$(CONFIG_SND_ALI5451) += snd.o snd-rawmidi.o snd-timer.o snd-pcm.o
obj-$(CONFIG_SND_CS46XX) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_EMU10K1) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_KORG1212) += snd-pcm.o snd-timer.o snd.o
obj-$(CONFIG_SND_NM256) += snd-pcm.o snd-timer.o snd.o
obj-$(CONFIG_SND_RME9652) += snd-pcm.o snd-timer.o snd.o
obj-$(CONFIG_SND_TRIDENT) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o
obj-$(CONFIG_SND_YMFPCI) += snd-pcm.o snd-timer.o snd.o snd-rawmidi.o snd-hwdep.o
obj-$(CONFIG_SND_POWERMAC) += snd-pcm.o snd-timer.o snd.o

include $(TOPDIR)/Rules.make

snd.o: $(snd-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-objs)

snd-pcm.o: $(snd-pcm-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-pcm-objs)

snd-rawmidi.o: $(snd-rawmidi-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-rawmidi-objs)

snd-timer.o: $(snd-timer-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-timer-objs)

snd-rtctimer.o: $(snd-rtctimer-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-rtctimer-objs)

snd-hwdep.o: $(snd-hwdep-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-hwdep-objs)
