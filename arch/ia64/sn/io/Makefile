#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2000 Silicon Graphics, Inc.
# Copyright (C) Jack Steiner (steiner@sgi.com)
#
#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

EXTRA_CFLAGS	:= -DSN -DLANGUAGE_C=1 -D_LANGUAGE_C=1 -I. -DBRINGUP \
		   -DDIRECT_L1_CONSOLE -DNUMA_BASE -DSIMULATED_KLGRAPH \
		   -DNUMA_MIGR_CONTROL -DLITTLE_ENDIAN -DREAL_HARDWARE \
		   -DNEW_INTERRUPTS -DCONFIG_IA64_SGI_IO
O_TARGET := sgiio.o
O_OBJS   := stubs.o sgi_if.o pciio.o pcibr.o xtalk.o xbow.o xswitch.o hubspc.o \
		klgraph_hack.o io.o hubdev.o \
		hcl.o labelcl.o invent.o klgraph.o klconflib.o sgi_io_sim.o \
		module.o sgi_io_init.o klgraph_hack.o ml_SN_init.o \
		ml_SN_intr.o ip37.o \
		ml_iograph.o hcl_util.o cdl.o \
		mem_refcnt.o devsupport.o alenlist.o pci_bus_cvlink.o \
		eeprom.o pci.o pci_dma.o l1.o l1_command.o

include $(TOPDIR)/Rules.make
