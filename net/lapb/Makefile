#
# Makefile for the Linux LAPB layer.
#

export-objs := lapb_iface.o

obj-$(CONFIG_LAPB) += lapb.o

lapb-objs := lapb_in.o lapb_out.o lapb_subr.o lapb_timer.o lapb_iface.o

include $(TOPDIR)/Rules.make

