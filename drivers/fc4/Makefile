#
# Makefile for the Linux Fibre Channel device drivers.
#

export-objs := fc_syms.o

fc4-objs := fc.o fc_syms.o

obj-$(CONFIG_FC4) += fc4.o
obj-$(CONFIG_FC4_SOC) += soc.o
obj-$(CONFIG_FC4_SOCAL) += socal.o

include $(TOPDIR)/Rules.make
