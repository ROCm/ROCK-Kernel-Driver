#
# Makefile for the Linux proc filesystem routines.
#

export-objs := root.o

obj-$(CONFIG_PROC_FS) += proc.o

proc-objs    := inode.o root.o base.o generic.o array.o \
		kmsg.o proc_tty.o proc_misc.o kcore.o

ifeq ($(CONFIG_PROC_DEVICETREE),y)
proc-objs    += proc_devtree.o
endif

include $(TOPDIR)/Rules.make
