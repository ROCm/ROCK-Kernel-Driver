#
# Makefile for the Linux AppleTalk layer.
#

export-objs = ddp.o

obj-$(CONFIG_ATALK) += appletalk.o

appletalk-y			:= aarp.o ddp.o
appletalk-$(CONFIG_SYSCTL)	+= sysctl_net_atalk.o
appletalk-objs			:= $(appletalk-y)

include $(TOPDIR)/Rules.make

