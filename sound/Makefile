# Makefile for the Linux sound card driver
#

O_TARGET := sound.o

export-objs     := sound_core.o

obj-$(CONFIG_SOUND) += soundcore.o
obj-$(CONFIG_SOUND_PRIME) += oss/
obj-$(CONFIG_SND) += core/ i2c/ drivers/ isa/ pci/ ppc/ arm/ synth/

ifeq ($(CONFIG_SND),y)
  obj-y += last.o
endif

soundcore-objs  := sound_core.o sound_firmware.o

include $(TOPDIR)/Rules.make
