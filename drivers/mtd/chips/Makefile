#
# linux/drivers/chips/Makefile
#
# $Id: Makefile,v 1.4 2001/06/09 19:57:57 dwmw2 Exp $

O_TARGET	:= chipslink.o

export-objs	:= chipreg.o

#                       *** BIG UGLY NOTE ***
#
# The removal of get_module_symbol() and replacement with
# inter_module_register() et al has introduced a link order dependency
# here where previously there was none.  We now have to ensure that
# the CFI command set drivers are linked before cfi_probe.o

obj-$(CONFIG_MTD)		+= chipreg.o
obj-$(CONFIG_MTD_AMDSTD)	+= amd_flash.o 
obj-$(CONFIG_MTD_CFI)		+= cfi_probe.o cfi_jedec.o
obj-$(CONFIG_MTD_CFI_AMDSTD)	+= cfi_cmdset_0002.o
obj-$(CONFIG_MTD_CFI_INTELEXT)	+= cfi_cmdset_0001.o
obj-$(CONFIG_MTD_JEDEC)		+= jedec.o
obj-$(CONFIG_MTD_RAM)		+= map_ram.o
obj-$(CONFIG_MTD_ROM)		+= map_rom.o
obj-$(CONFIG_MTD_SHARP)		+= sharp.o

include $(TOPDIR)/Rules.make
