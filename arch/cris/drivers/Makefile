#
# Makefile for Etrax-specific drivers
#

O_TARGET := drivers.o

obj-y :=

obj-$(CONFIG_ETRAX_ETHERNET)     += ethernet.o
obj-$(CONFIG_ETRAX_SERIAL)       += serial.o
obj-$(CONFIG_ETRAX_IDE)          += ide.o
obj-$(CONFIG_ETRAX_AXISFLASHMAP) += axisflashmap.o

include $(TOPDIR)/Rules.make
