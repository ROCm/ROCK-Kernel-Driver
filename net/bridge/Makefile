#
# Makefile for the IEEE 802.1d ethernet bridging layer.
#

export-objs := br.o

obj-$(CONFIG_BRIDGE) += bridge.o

bridge-objs	:= br.o br_device.o br_fdb.o br_forward.o br_if.o br_input.o \
			br_ioctl.o br_notify.o br_stp.o br_stp_bpdu.o \
			br_stp_if.o br_stp_timer.o

ifeq ($(CONFIG_NETFILTER),y)
bridge-objs	+= br_netfilter.o
endif

obj-$(CONFIG_BRIDGE_NF_EBTABLES) += netfilter/

include $(TOPDIR)/Rules.make
