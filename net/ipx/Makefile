#
# Makefile for the Linux IPX layer.
#

export-objs = af_ipx.o af_spx.o

obj-$(CONFIG_IPX) += ipx.o

ipx-y			:= af_ipx.o
ipx-$(CONFIG_SYSCTL)	+= sysctl_net_ipx.o
ipx-$(CONFIG_SPX)	+= af_spx.o
ipx-objs		:= $(ipx-y)

include $(TOPDIR)/Rules.make
