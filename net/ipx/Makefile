#
# Makefile for the Linux IPX layer.
#

obj-$(CONFIG_IPX) += ipx.o

ipx-y			:= af_ipx.o
ipx-$(CONFIG_SYSCTL)	+= sysctl_net_ipx.o
ipx-objs		:= $(ipx-y)

include $(TOPDIR)/Rules.make
