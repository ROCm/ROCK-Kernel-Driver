#
# linux/drivers/nand/Makefile
#
# $Id: Makefile,v 1.4 2001/06/28 10:49:45 dwmw2 Exp $

O_TARGET	:= nandlink.o

export-objs	:= nand.o nand_ecc.o

obj-$(CONFIG_MTD_NAND)		+= nand.o
obj-$(CONFIG_MTD_NAND_ECC)	+= nand_ecc.o
obj-$(CONFIG_MTD_NAND_SPIA)	+= spia.o

include $(TOPDIR)/Rules.make
