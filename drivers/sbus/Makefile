#
# Makefile for the linux kernel.
#

ifneq ($(ARCH),m68k)
obj-y    := sbus.o dvma.o
endif

subdir-y += char
subdir-m += char
obj-$(CONFIG_SBUSCHAR) += char/sunchar.o

subdir-y += audio
subdir-m += audio

# This is grotty but works around some problems with modules.
ifeq ($(CONFIG_SPARCAUDIO),y)
obj-y += audio/sparcaudio.o
endif

include $(TOPDIR)/Rules.make
