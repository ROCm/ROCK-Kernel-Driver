#
# Makefile for the Linux WAN router layer.
#

export-objs := wanmain.o

obj-$(CONFIG_WAN_ROUTER) += wanrouter.o

wanrouter-objs :=  wanproc.o wanmain.o

include $(TOPDIR)/Rules.make
