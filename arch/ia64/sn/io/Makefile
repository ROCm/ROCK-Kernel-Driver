# arch/ia64/sn/io/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
#
# Makefile for the sn io routines.
#

EXTRA_CFLAGS    := -DLITTLE_ENDIAN -DSHUB_SWAP_WAR

obj-$(CONFIG_IA64_SGI_SN) += sgi_if.o xswitch.o sgi_io_sim.o cdl.o ate_utils.o \
			     io.o machvec/ drivers/ platform_init/

obj-$(CONFIG_IA64_SGI_SN2) += sn2/

ifdef CONFIG_HWGFS_FS
obj-$(CONFIG_HWGFS_FS) += hwgfs/
else
obj-$(CONFIG_DEVFS_FS) += hwgdfs/
endif
