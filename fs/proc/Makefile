#
# Makefile for the Linux proc filesystem routines.
#

obj-$(CONFIG_PROC_FS) += proc.o

proc-y			:= task_nommu.o
proc-$(CONFIG_MMU)	:= task_mmu.o

proc-y       += inode.o root.o base.o generic.o array.o \
		kmsg.o proc_tty.o proc_misc.o

proc-$(CONFIG_PROC_KCORE)	+= kcore.o
proc-$(CONFIG_PROC_DEVICETREE)	+= proc_devtree.o
