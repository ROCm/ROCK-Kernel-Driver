#
# linux/drivers/nand/Makefile
#
# $Id: Makefile.common,v 1.9 2004/07/12 16:07:31 dwmw2 Exp $

obj-$(CONFIG_MTD_NAND)			+= nand.o nand_ecc.o
obj-$(CONFIG_MTD_NAND_IDS)		+= nand_ids.o

obj-$(CONFIG_MTD_NAND_SPIA)		+= spia.o
obj-$(CONFIG_MTD_NAND_TOTO)		+= toto.o
obj-$(CONFIG_MTD_NAND_AUTCPU12)		+= autcpu12.o
obj-$(CONFIG_MTD_NAND_EDB7312)		+= edb7312.o
obj-$(CONFIG_MTD_NAND_TX4925NDFMC)	+= tx4925ndfmc.o
obj-$(CONFIG_MTD_NAND_TX4938NDFMC)	+= tx4938ndfmc.o
obj-$(CONFIG_MTD_NAND_AU1550)		+= au1550nd.o
obj-$(CONFIG_MTD_NAND_PPCHAMELEONEVB)	+= ppchameleonevb.o
obj-$(CONFIG_MTD_NAND_DISKONCHIP)	+= diskonchip.o

nand-objs = nand_base.o nand_bbt.o
