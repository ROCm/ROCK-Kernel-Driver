#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := drivers.o

subdir-y     := opl3 mpu401
subdir-m     := $(subdir-y)

list-multi   := snd-dummy.o snd-mtpav.o snd-serial-u16550.o snd-virmidi.o

snd-dummy-objs := dummy.o
snd-mtpav-objs := mtpav.o
snd-serial-u16550-objs := serial-u16550.o
snd-virmidi-objs := virmidi.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_DUMMY) += snd-dummy.o
obj-$(CONFIG_SND_VIRMIDI) += snd-virmidi.o
obj-$(CONFIG_SND_SERIAL_U16550) += snd-serial-u16550.o
obj-$(CONFIG_SND_MTPAV) += snd-mtpav.o

include $(TOPDIR)/Rules.make

snd-dummy.o: $(snd-dummy-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-dummy-objs)

snd-mtpav.o: $(snd-mtpav-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-mtpav-objs)

snd-serial-u16550.o: $(snd-serial-u16550-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-serial-u16550-objs)

snd-virmidi.o: $(snd-virmidi-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-virmidi-objs)
