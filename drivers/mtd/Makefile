#
# Makefile for the memory technology device drivers.
#
# Based on:
# $Id: Makefile,v 1.66 2002/04/23 13:52:14 mag Exp $

export-objs	:= mtdcore.o mtdpart.o redboot.o cmdline.o afs.o mtdconcat.o

obj-y           += chips/ maps/ devices/ nand/

#                       *** BIG UGLY NOTE ***
#
# The shiny new inter_module_xxx has introduced yet another ugly link
# order dependency, which I'd previously taken great care to avoid.
# We now have to ensure that the chip drivers are initialised before the
# map drivers, and that the doc200[01] drivers are initialised before
# docprobe.
#
# We'll hopefully merge the doc200[01] drivers and docprobe back into
# a single driver some time soon, but the CFI drivers are going to have
# to stay like that.
#
# Urgh.
# 
# dwmw2 21/11/0

# Core functionality.
obj-$(CONFIG_MTD)		+= mtdcore.o
obj-$(CONFIG_MTD_CONCAT)	+= mtdconcat.o
obj-$(CONFIG_MTD_PARTITIONS)	+= mtdpart.o
obj-$(CONFIG_MTD_REDBOOT_PARTS) += redboot.o
obj-$(CONFIG_MTD_CMDLINE_PARTS) += cmdline.o
obj-$(CONFIG_MTD_AFS_PARTS)	+= afs.o

# 'Users' - code which presents functionality to userspace.
obj-$(CONFIG_MTD_CHAR)		+= mtdchar.o
obj-$(CONFIG_MTD_BLOCK)		+= mtdblock.o
obj-$(CONFIG_MTD_BLOCK_RO)	+= mtdblock_ro.o
obj-$(CONFIG_FTL)		+= ftl.o
obj-$(CONFIG_NFTL)		+= nftl.o

nftl-objs	:= nftlcore.o nftlmount.o

include $(TOPDIR)/Rules.make
