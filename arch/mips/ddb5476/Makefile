#
# Makefile for the NEC DDB Vrc-5074 specific kernel interface routines
# under Linux.
#

EXTRA_AFLAGS := $(CFLAGS)

obj-y				+= setup.o irq.o time.o prom.o pci.o \
				   int-handler.o nile4.o
obj-$(CONFIG_REMOTE_DEBUG)	+= dbg_io.o

include $(TOPDIR)/Rules.make
