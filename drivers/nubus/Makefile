#
# Makefile for the nubus specific drivers.
#

export-objs := nubus_syms.o

obj-y   := nubus.o

obj-$(CONFIG_MODULES) += nubus_syms.o 
obj-$(CONFIG_PROC_FS) += proc.o

include $(TOPDIR)/Rules.make
