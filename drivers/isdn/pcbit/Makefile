# Makefile for the pcbit ISDN device driver

# The target object and module list name.

O_TARGET	:= built-in.o

# Multipart objects.

pcbit-objs	:= module.o edss1.o drv.o layer2.o capi.o callbacks.o

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_DRV_PCBIT)	+= pcbit.o

include $(TOPDIR)/Rules.make
