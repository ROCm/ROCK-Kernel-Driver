#
# Makefile for the linux kernel.
#

ifneq ($(ARCH),m68k)
obj-y    := sbus.o dvma.o
endif

mod-subdirs := char audio

obj-$(CONFIG_SBUSCHAR) += char/
obj-$(CONFIG_SPARCAUDIO) += audio/

include $(TOPDIR)/Rules.make
