#
# Makefile for the Linux AppleTalk layer.
#

obj-$(CONFIG_ATALK) += appletalk.o

appletalk-y			:= aarp.o ddp.o atalk_proc.o
appletalk-$(CONFIG_SYSCTL)	+= sysctl_net_atalk.o
