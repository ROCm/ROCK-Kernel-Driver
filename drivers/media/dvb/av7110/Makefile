#
# Makefile for the kernel AV7110 DVB device driver
#

dvb-ttpci-objs := saa7146_core.o saa7146_v4l.o av7110.o av7110_ir.o

obj-$(CONFIG_DVB_AV7110) += dvb-ttpci.o

EXTRA_CFLAGS = -Idrivers/media/dvb/dvb-core/

include $(TOPDIR)/Rules.make 

