# Makefile for the Linux sound card driver
#

O_TARGET := sound.o

export-objs     := sound_core.o

obj-$(CONFIG_SOUND) += soundcore.o

subdir-$(CONFIG_SOUND_PRIME) += oss

ifeq ($(CONFIG_SOUND_PRIME),y)
  obj-y += oss/sounddrivers.o
endif

subdir-$(CONFIG_SND) += core i2c drivers isa pci ppc synth

ifeq ($(CONFIG_SND),y)
  obj-y += core/core.o i2c/_i2c.o
  obj-y += drivers/drivers.o \
	   drivers/mpu401/_mpu401.o \
	   drivers/opl3/_opl3.o
  obj-y += isa/isa.o \
	   isa/ad1816a/_ad1816a.o \
           isa/ad1848/_ad1848.o \
	   isa/cs423x/_cs423x.o \
	   isa/es1688/_es1688.o \
	   isa/gus/_gus.o \
	   isa/opti9xx/_opti9xx.o \
	   isa/sb/_sb.o \
	   isa/wavefront/_wavefront.o
  obj-y += pci/pci.o \
	   pci/ac97/_ac97.o \
	   pci/ali5451/_ali5451.o \
	   pci/cs46xx/_cs46xx.o \
	   pci/emu10k1/_emu10k1.o \
	   pci/korg1212/_korg1212.o \
	   pci/nm256/_nm256.o \
	   pci/rme9652/_rme9652.o \
	   pci/trident/_trident.o \
	   pci/ymfpci/_ymfpci.o
  obj-y += ppc/ppc.o
  obj-y += synth/synth.o \
	   synth/emux/_emux.o
  obj-y += last.o
endif

list-multi      := soundcore.o

soundcore-objs  := sound_core.o sound_firmware.o


include $(TOPDIR)/Rules.make


soundcore.o: $(soundcore-objs)
	$(LD) -r -o $@ $(soundcore-objs)
