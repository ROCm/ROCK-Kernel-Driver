# arch/ia64/sn/io/sn2/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2002-2003 Silicon Graphics, Inc.  All Rights Reserved.
#
# Makefile for the sn2 specific io routines.
#

EXTRA_CFLAGS := -DLITTLE_ENDIAN

obj-y += pcibr/ bte_error.o geo_op.o klconflib.o klgraph.o l1.o \
	 l1_command.o ml_iograph.o ml_SN_init.o ml_SN_intr.o module.o \
	 pci_bus_cvlink.o pciio.o pic.o sgi_io_init.o shub.o shuberror.o \
	 shub_intr.o shubio.o xbow.o xtalk.o

obj-$(CONFIG_KDB) += kdba_io.o 
obj-$(CONFIG_SHUB_1_0_SPECIFIC) += efi-rtc.o
