#
# linux/drivers/devices/Makefile
#
# $Id: Makefile.common,v 1.3 2003/05/28 10:54:23 dwmw2 Exp $

#                       *** BIG UGLY NOTE ***
#
# The removal of get_module_symbol() and replacement with
# inter_module_register() et al has introduced a link order dependency
# here where previously there was none.  We now have to ensure that
# doc200[01].o are linked before docprobe.o

obj-$(CONFIG_MTD_DOC2000)	+= doc2000.o
obj-$(CONFIG_MTD_DOC2001)	+= doc2001.o
obj-$(CONFIG_MTD_DOC2001PLUS)	+= doc2001plus.o
obj-$(CONFIG_MTD_DOCPROBE)	+= docprobe.o docecc.o
obj-$(CONFIG_MTD_SLRAM)		+= slram.o
obj-$(CONFIG_MTD_PMC551)	+= pmc551.o
obj-$(CONFIG_MTD_MS02NV)	+= ms02-nv.o
obj-$(CONFIG_MTD_MTDRAM)	+= mtdram.o
obj-$(CONFIG_MTD_LART)		+= lart.o
obj-$(CONFIG_MTD_BLKMTD)	+= blkmtd.o
