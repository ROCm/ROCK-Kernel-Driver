#
# Makefile for Linux arch/m68k/amiga source directory
#

export-objs	:= amiga_ksyms.o

obj-y		:= config.o amiints.o cia.o time.o bootinfo.o amisound.o \
			chipram.o amiga_ksyms.o

obj-$(CONFIG_AMIGA_PCMCIA) += pcmcia.o

include $(TOPDIR)/Rules.make
