#
# Makefile for the memory technology device drivers.
#
# $Id: Makefile.common,v 1.3 2004/07/12 16:07:30 dwmw2 Exp $

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
