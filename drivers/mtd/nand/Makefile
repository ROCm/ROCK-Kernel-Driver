#
# linux/drivers/nand/Makefile
#
# $Id: Makefile,v 1.3 2001/04/19 23:54:48 dwmw2 Exp $

O_TARGET	:= nandlink.o

export-objs	:= nand.o

obj-$(CONFIG_MTD_NAND)		+= nand.o
obj-$(CONFIG_MTD_NAND_ECC)	+= nand_ecc.o
obj-$(CONFIG_MTD_NAND_SPIA)	+= spia.o

include $(TOPDIR)/Rules.make
