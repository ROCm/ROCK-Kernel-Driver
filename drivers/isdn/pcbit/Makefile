# Makefile for the pcbit ISDN device driver

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_DRV_PCBIT)	+= pcbit.o

# Multipart objects.

pcbit-objs	:= module.o edss1.o drv.o layer2.o capi.o callbacks.o

include $(TOPDIR)/Rules.make
