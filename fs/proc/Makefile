#
# Makefile for the Linux proc filesystem routines.
#

obj-$(CONFIG_PROC_FS) += proc.o

proc-objs    := inode.o root.o base.o generic.o array.o \
		kmsg.o proc_tty.o proc_misc.o kcore.o

ifeq ($(CONFIG_MMU),y)
proc-objs    += task_mmu.o
else
proc-objs    += task_nommu.o
endif

ifeq ($(CONFIG_PROC_DEVICETREE),y)
proc-objs    += proc_devtree.o
endif
