#
# Makefile for the Linux Ethernet layer.
#

OBJS-y	:= eth.o

OBJS-$(CONFIG_SYSCTL)	+= sysctl_net_ether.o
OBJS-$(CONFIG_IPX)	+= pe2.o
OBJS-$(CONFIG_ATALK)	+= pe2.o

obj-$(CONFIG_NET)	:= $(OBJS-y)

include $(TOPDIR)/Rules.make
