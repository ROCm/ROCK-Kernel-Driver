#
# Makefile for the memory technology device drivers.
#
# $Id: Makefile.common,v 1.2 2003/05/23 11:38:29 dwmw2 Exp $

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
obj-$(CONFIG_MTD_CMDLINE_PARTS) += cmdlinepart.o
obj-$(CONFIG_MTD_AFS_PARTS)	+= afs.o

# 'Users' - code which presents functionality to userspace.
obj-$(CONFIG_MTD_CHAR)		+= mtdchar.o
obj-$(CONFIG_MTD_BLOCK)		+= mtdblock.o mtd_blkdevs.o
obj-$(CONFIG_MTD_BLOCK_RO)	+= mtdblock_ro.o mtd_blkdevs.o
obj-$(CONFIG_FTL)		+= ftl.o mtd_blkdevs.o
obj-$(CONFIG_NFTL)		+= nftl.o mtd_blkdevs.o
obj-$(CONFIG_INFTL)		+= inftl.o mtd_blkdevs.o

nftl-objs		:= nftlcore.o nftlmount.o
inftl-objs		:= inftlcore.o inftlmount.o

obj-y		+= chips/ maps/ devices/ nand/
