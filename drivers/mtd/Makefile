#
# Makefile for the memory technology device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#
# $Id: Makefile,v 1.22 2000/07/14 08:10:52 dwmw2 Exp $

# Object file lists.

obj-y           :=
obj-m           :=
obj-n           :=
obj-            :=

O_TARGET	:= mtdlink.o
SUB_DIRS	:=
ALL_SUB_DIRS 	:=
MOD_SUB_DIRS	:=

export-objs	:=	mtdcore.o mtdpart.o jedec.o
list-multi	:=

# MTD devices
obj-$(CONFIG_MTD)		+= mtdcore.o
obj-$(CONFIG_MTD_DOC1000)	+= doc1000.o
obj-$(CONFIG_MTD_DOC2000)	+= doc2000.o
obj-$(CONFIG_MTD_DOC2001)	+= doc2001.o
obj-$(CONFIG_MTD_DOCPROBE)	+= docprobe.o docecc.o
obj-$(CONFIG_MTD_SLRAM)		+= slram.o
obj-$(CONFIG_MTD_PMC551)	+= pmc551.o
obj-$(CONFIG_MTD_MTDRAM)	+= mtdram.o

# Chip drivers
obj-$(CONFIG_MTD_JEDEC)		+= jedec.o
obj-$(CONFIG_MTD_RAM)		+= map_ram.o
obj-$(CONFIG_MTD_ROM)		+= map_rom.o
obj-$(CONFIG_MTD_CFI)		+= cfi_probe.o
obj-$(CONFIG_MTD_CFI_INTELEXT)	+= cfi_cmdset_0001.o
obj-$(CONFIG_MTD_CFI_AMDSTD)	+= cfi_cmdset_0002.o

# Chip mappings
obj-$(CONFIG_MTD_PHYSMAP)	+= physmap.o
obj-$(CONFIG_MTD_MIXMEM)	+= mixmem.o
obj-$(CONFIG_MTD_NORA)		+= nora.o
obj-$(CONFIG_MTD_OCTAGON)	+= octagon-5066.o
obj-$(CONFIG_MTD_PNC2000)	+= pnc2000.o mtdpart.o
obj-$(CONFIG_MTD_RPXLITE)	+= rpxlite.o
obj-$(CONFIG_MTD_VMAX)		+= vmax301.o

# Users
obj-$(CONFIG_MTD_CHAR)		+= mtdchar.o
obj-$(CONFIG_MTD_BLOCK)		+= mtdblock.o
obj-$(CONFIG_FTL)		+= ftl.o
obj-$(CONFIG_NFTL)		+= nftl.o nftlmount.o

include $(TOPDIR)/Rules.make
