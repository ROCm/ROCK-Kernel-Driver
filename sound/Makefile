# Makefile for the Linux sound card driver
#

O_TARGET := sound.o

export-objs     := sound_core.o

mod-subdirs	:= oss core i2c drivers isa pci ppc synth

obj-$(CONFIG_SOUND) += soundcore.o

subdir-$(CONFIG_SOUND_PRIME) += oss
ifeq ($(CONFIG_SOUND_PRIME),y)
  obj-y += oss/sounddrivers.o
endif

subdir-$(CONFIG_SND) += core i2c drivers isa pci ppc arm synth
ifeq ($(CONFIG_SND),y)
  obj-y += core/core.o i2c/_i2c.o
  obj-y += drivers/drivers.o
  obj-y += isa/isa.o
  obj-y += pci/pci.o
  obj-y += arm/arm.o
  obj-y += ppc/ppc.o
  obj-y += synth/synth.o
  obj-y += last.o
endif

soundcore-objs  := sound_core.o sound_firmware.o


include $(TOPDIR)/Rules.make
