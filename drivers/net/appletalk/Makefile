#
# Makefile for drivers/net/appletalk
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

O_TARGET := built-in.o

obj-y   :=
obj-n   :=
obj-m   :=
obj-    :=
export-objs :=

obj-$(CONFIG_IPDDP) += ipddp.o
obj-$(CONFIG_COPS) += cops.o
obj-$(CONFIG_LTPC) += ltpc.o

include $(TOPDIR)/Rules.make

