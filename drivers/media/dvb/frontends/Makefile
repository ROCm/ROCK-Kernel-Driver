#
# Makefile for the kernel DVB frontend device drivers.
#

EXTRA_CFLAGS = -Idrivers/media/dvb/dvb-core/

obj-$(CONFIG_DVB_ALPS_BSRU6) += alps_bsru6.o
obj-$(CONFIG_DVB_ALPS_BSRV2) += alps_bsrv2.o
obj-$(CONFIG_DVB_GRUNDIG_29504_491) += grundig_29504-491.o
obj-$(CONFIG_DVB_GRUNDIG_29504_401) += grundig_29504-401.o
obj-$(CONFIG_DVB_VES1820) += ves1820.o

include $(TOPDIR)/Rules.make 
 
