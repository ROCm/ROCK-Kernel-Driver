#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= core.o
obj-m			:=
obj-n			:=
obj-			:=

export-objs		:= 

obj-$(CONFIG_PCI)	+= pci.o
obj-$(CONFIG_LEDS)	+= leds.o

include $(TOPDIR)/Rules.make
