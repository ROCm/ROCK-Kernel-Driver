# arch/ia64/sn/kernel/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1999,2001-2003 Silicon Graphics, Inc.  All Rights Reserved.
#

obj-y				+= probe.o setup.o bte.o irq.o mca.o idle.o sn2/
obj-$(CONFIG_IA64_GENERIC)      += machvec.o
obj-$(CONFIG_IA64_SGI_SN_XPC)	+= xp.o
xp-y				:= xp_main.o xp_nofault.o
obj-$(CONFIG_IA64_SGI_SN_XPC)	+= xpc.o
xpc-y				:= xpc_main.o xpc_kdb.o xpc_channel.o xpc_partition.o
