#
# linux/drivers/nand/Makefile
#
# $Id: Makefile,v 1.5 2001/09/19 22:39:59 dwmw2 Exp $

O_TARGET	:= nandlink.o

export-objs	:= nand.o nand_ecc.o

nandobjs-y			:= nand.o
nandobjs-$(CONFIG_MTD_NAND_ECC) += nand_ecc.o

obj-$(CONFIG_MTD_NAND)		+= $(nandobjs-y)
obj-$(CONFIG_MTD_NAND_SPIA)	+= spia.o

include $(TOPDIR)/Rules.make
