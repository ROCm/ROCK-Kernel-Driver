#
# Makefile for ALSA
# Copyright (c) 1999 by Jaroslav Kysela <perex@suse.cz>
#

obj-$(CONFIG_SND) += instr/
ifeq ($(CONFIG_SND_SEQUENCER_OSS),y)
  obj-$(CONFIG_SND_SEQUENCER) += oss/
endif

snd-seq-device-objs := seq_device.o
snd-seq-objs := seq.o seq_lock.o seq_clientmgr.o seq_memory.o seq_queue.o \
                seq_fifo.o seq_prioq.o seq_timer.o \
                seq_system.o seq_ports.o seq_info.o
snd-seq-midi-objs := seq_midi.o
snd-seq-midi-emul-objs := seq_midi_emul.o
snd-seq-midi-event-objs := seq_midi_event.o
snd-seq-instr-objs := seq_instr.o
snd-seq-dummy-objs := seq_dummy.o
snd-seq-virmidi-objs := seq_virmidi.o

RAWMIDI_OBJS = snd-seq-midi.o snd-seq-midi-event.o
OPL3_OBJS = snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o

#
# this function returns:
#   "m" - CONFIG_SND_SEQUENCER is m
#   <empty string> - CONFIG_SND_SEQUENCER is undefined
#   otherwise parameter #1 value
#
sequencer = $(if $(subst y,,$(CONFIG_SND_SEQUENCER)),$(if $(1),m),$(if $(CONFIG_SND_SEQUENCER),$(1)))

obj-$(CONFIG_SND_SEQUENCER) += snd-seq.o snd-seq-device.o
ifeq ($(CONFIG_SND_SEQUENCER_OSS),y)
obj-$(CONFIG_SND_SEQUENCER) += snd-seq-midi-event.o
endif
obj-$(CONFIG_SND_SEQ_DUMMY) += snd-seq-dummy.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_VIRMIDI) += snd-seq-virmidi.o snd-seq-midi-event.o
obj-$(call sequencer,$(CONFIG_SND_SERIAL_U16550)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_MTPAV)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_MPU401)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_ALS100)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_AZT2320)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_AZT3328)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_DT019X)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_ES18XX)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_OPL3SA2)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_AD1816A)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_CS4231)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_CS4232)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_CS4236)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_PC98_CS4232)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_ES1688)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_GUSCLASSIC)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_GUSMAX)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_GUSEXTREME)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_INTERWAVE)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_INTERWAVE_STB)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_OPTI92X_AD1848)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_OPTI92X_CS4231)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_OPTI93X)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_SB8)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_SB16)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_SBAWE)) += $(RAWMIDI_OBJS) $(OPL3_OBJS) snd-seq-virmidi.o
obj-$(call sequencer,$(CONFIG_SND_ES968)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_WAVEFRONT)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_SSCAPE)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_ALS4000)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_CMIPCI)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_CS4281)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_ENS1370)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_ENS1371)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_ES1938)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_ES1968)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_FM801)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_ICE1712)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_ICE1724)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_INTEL8X0)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_SONICVIBES)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_VIA82XX)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_ALI5451)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_CS46XX)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_EMU10K1)) += $(RAWMIDI_OBJS) snd-seq-midi-emul.o snd-seq-virmidi.o
obj-$(call sequencer,$(CONFIG_SND_TRIDENT)) += $(RAWMIDI_OBJS) snd-seq-midi-emul.o snd-seq-instr.o
obj-$(call sequencer,$(CONFIG_SND_YMFPCI)) += $(RAWMIDI_OBJS) $(OPL3_OBJS)
obj-$(call sequencer,$(CONFIG_SND_USB_AUDIO)) += $(RAWMIDI_OBJS)
obj-$(call sequencer,$(CONFIG_SND_HDSP)) += $(RAWMIDI_OBJS)

obj-m := $(sort $(obj-m))
