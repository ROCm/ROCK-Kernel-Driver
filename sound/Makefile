# Makefile for the Linux sound card driver
#

O_TARGET := sound.o

export-objs     := sound_core.o

mod-subdirs	:= oss core i2c drivers isa pci ppc synth

obj-$(CONFIG_SOUND) += soundcore.o

subdir-$(CONFIG_SOUND_PRIME) += oss
ifeq ($(CONFIG_SOUND_PRIME),y)
  obj-y += oss/_oss.o
endif

subdir-$(CONFIG_SND) += core i2c drivers isa pci ppc arm synth
ifeq ($(CONFIG_SND),y)
  obj-y += core/_core.o i2c/_i2c.o
  obj-y += drivers/_drivers.o
  obj-y += isa/_isa.o
  obj-y += pci/_pci.o
  obj-y += arm/_arm.o
  obj-y += ppc/_ppc.o
  obj-y += synth/_synth.o
  obj-y += last.o
endif

soundcore-objs  := sound_core.o sound_firmware.o


include $(TOPDIR)/Rules.make
