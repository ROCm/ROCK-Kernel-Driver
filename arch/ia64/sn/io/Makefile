#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
#
#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

EXTRA_CFLAGS    := -DLITTLE_ENDIAN

O_TARGET := sgiio.o

ifeq ($(CONFIG_MODULES),y)
export-objs = pciio.o hcl.o
endif

obj-y  := stubs.o sgi_if.o pciio.o xtalk.o xbow.o xswitch.o klgraph_hack.o \
		hcl.o labelcl.o invent.o klgraph.o klconflib.o sgi_io_sim.o \
		module.o sgi_io_init.o klgraph_hack.o ml_SN_init.o \
		ml_iograph.o hcl_util.o cdl.o hubdev.o hubspc.o \
		alenlist.o pci_bus_cvlink.o \
		eeprom.o pci.o pci_dma.o l1.o l1_command.o ate_utils.o \
		ifconfig_net.o efi-rtc.o io.o

obj-$(CONFIG_IA64_SGI_SN1)   += sn1/ml_SN_intr.o sn1/mem_refcnt.o sn1/hubcounters.o \
				sn1/ip37.o sn1/huberror.o sn1/hub_intr.o sn1/pcibr.o

obj-$(CONFIG_IA64_SGI_SN2)   += sn2/ml_SN_intr.o sn2/shub_intr.o sn2/shuberror.o \
				sn2/bte_error.o \
				sn2/pcibr/pcibr_dvr.o sn2/pcibr/pcibr_ate.o \
				sn2/pcibr/pcibr_config.o sn2/pcibr/pcibr_dvr.o \
				sn2/pcibr/pcibr_hints.o \
				sn2/pcibr/pcibr_idbg.o sn2/pcibr/pcibr_intr.o \
				sn2/pcibr/pcibr_rrb.o sn2/pcibr/pcibr_slot.o 

obj-$(CONFIG_PCIBA) += pciba.o

include $(TOPDIR)/Rules.make
