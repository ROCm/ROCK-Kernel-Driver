#
# Makefile for Linux arch/m68k/atari source directory
#

export-objs	:= atari_ksyms.o

obj-y		:= config.o time.o debug.o ataints.o stdma.o \
			atasound.o stram.o atari_ksyms.o

obj-$(CONFIG_VT)	+= atakeyb.o joystick.o

ifeq ($(CONFIG_PCI),y)
obj-$(CONFIG_HADES)	+= hades-pci.o
endif

include $(TOPDIR)/Rules.make
