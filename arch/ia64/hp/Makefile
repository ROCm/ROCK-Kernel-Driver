#
# ia64/platform/hp/Makefile
#
# Copyright (C) 2002 Hewlett-Packard Co.
#	David Mosberger-Tang <davidm@hpl.hp.com>
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

all: hp.a

O_TARGET := hp.a

obj-y := hpsim_console.o hpsim_irq.o hpsim_setup.o
obj-$(CONFIG_IA64_GENERIC) += hpsim_machvec.o

obj-$(CONFIG_SIMETH)	+= simeth.o
obj-$(CONFIG_SIM_SERIAL) += simserial.o
obj-$(CONFIG_SCSI_SIM)	+= simscsi.o

clean::

include $(TOPDIR)/Rules.make
