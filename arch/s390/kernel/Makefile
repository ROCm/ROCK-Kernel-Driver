#
# Makefile for the linux kernel.
#

O_TARGET	:= kernel.o
EXTRA_TARGETS	:= head.o init_task.o

export-objs	:= debug.o ebcdic.o irq.o s390_ext.o smp.o s390_ksyms.o
obj-y	:= entry.o bitmap.o traps.o time.o process.o irq.o \
            setup.o sys_s390.o ptrace.o signal.o cpcmd.o ebcdic.o \
            semaphore.o s390fpu.o reipl.o s390_ext.o debug.o

obj-$(CONFIG_MODULES)		+= s390_ksyms.o
obj-$(CONFIG_SMP)		+= smp.o

#
# Kernel debugging
#
obj-$(CONFIG_REMOTE_DEBUG)	+= gdb-stub.o #gdb-low.o 

EXTRA_AFLAGS := -traditional

include $(TOPDIR)/Rules.make

