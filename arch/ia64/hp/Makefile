#
# ia64/platform/hp/Makefile
#
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

all: hp.a

O_TARGET := hp.a

obj-y := hpsim_console.o hpsim_irq.o hpsim_setup.o
obj-$(CONFIG_IA64_GENERIC) += hpsim_machvec.o

clean::

include $(TOPDIR)/Rules.make
