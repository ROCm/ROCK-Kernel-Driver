#
# Makefile for drivers/telephony
#

export-objs := phonedev.o ixj.o

obj-$(CONFIG_PHONE) += phonedev.o
obj-$(CONFIG_PHONE_IXJ) += ixj.o
obj-$(CONFIG_PHONE_IXJ_PCMCIA) += ixj_pcmcia.o

include $(TOPDIR)/Rules.make

