#
# Makefile for ALSA
# Copyright (c) 1999 by Jaroslav Kysela <perex@suse.cz>
#

obj-$(CONFIG_SND) += instr/
ifeq ($(CONFIG_SND_SEQUENCER_OSS),y)
  obj-$(CONFIG_SND_SEQUENCER) += oss/
endif

export-objs  := seq_device.o seq.o seq_instr.o seq_midi_emul.o \
		seq_midi_event.o seq_virmidi.o

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

obj-$(CONFIG_SND_SEQUENCER) += snd-seq.o snd-seq-device.o
ifeq ($(CONFIG_SND_SEQUENCER_OSS),y)
obj-$(CONFIG_SND_SEQUENCER) += snd-seq-midi-event.o
endif
obj-$(CONFIG_SND_SEQ_DUMMY) += snd-seq-dummy.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_VIRMIDI) += snd-seq-virmidi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_SERIAL_U16550) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_MTPAV) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_MPU401) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_ALS100) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_AZT2320) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_DT019X) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_ES18XX) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_OPL3SA2) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_AD1816A) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_CS4231) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_CS4232) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_CS4236) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_ES1688) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_GUSCLASSIC) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_GUSMAX) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_GUSEXTREME) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_INTERWAVE) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_INTERWAVE_STB) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_OPTI92X_AD1848) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_OPTI92X_CS4231) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_OPTI93X) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_SB8) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_SB16) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_SBAWE) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o snd-seq-virmidi.o
obj-$(CONFIG_SND_ES968) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_WAVEFRONT) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_ALS4000) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_CMIPCI) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_CS4281) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_ENS1370) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_ENS1371) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_ES1938) += snd-seq-device.o snd-seq-midi-emul.o snd-seq.o snd-seq-instr.o snd-seq-midi.o snd-seq-midi-event.o
obj-$(CONFIG_SND_ES1968) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_FM801) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_ICE1712) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_INTEL8X0) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_SONICVIBES) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_VIA82XX) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_ALI5451) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_CS46XX) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o
obj-$(CONFIG_SND_EMU10K1) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-virmidi.o
obj-$(CONFIG_SND_TRIDENT) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_YMFPCI) += snd-seq-midi.o snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-midi-emul.o snd-seq-instr.o
obj-$(CONFIG_SND_USB_AUDIO) += snd-seq.o snd-seq-device.o snd-seq-midi-event.o snd-seq-virmidi.o

obj-m := $(sort $(obj-m))

include $(TOPDIR)/Rules.make
