#
# drivers/net/wireless/Makefile
#
# Makefile for the Linux Wireless network device drivers.
#

O_TARGET := orinoco_drvs.o

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

# Things that need to export symbols
export-objs	:= orinoco.o hermes.o

# ISA Bus cards

# PCI bus cards

# Other cards
obj-$(CONFIG_APPLE_AIRPORT)	+= airport.o orinoco.o hermes.o

# 16-bit Pcmcia wireless client drivers
obj-$(CONFIG_PCMCIA_HERMES)	+= orinoco_cs.o orinoco.o hermes.o

include $(TOPDIR)/Rules.make
