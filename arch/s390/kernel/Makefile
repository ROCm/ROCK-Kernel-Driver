#
# Makefile for the linux kernel.
#

EXTRA_TARGETS	:= head.o init_task.o
EXTRA_AFLAGS	:= -traditional

obj-y	:= entry.o bitmap.o traps.o time.o process.o \
            setup.o sys_s390.o ptrace.o signal.o cpcmd.o ebcdic.o \
            semaphore.o reipl.o s390_ext.o debug.o

obj-$(CONFIG_MODULES)		+= s390_ksyms.o module.o
obj-$(CONFIG_SMP)		+= smp.o

#
# Kernel debugging
#
obj-$(CONFIG_REMOTE_DEBUG)	+= gdb-stub.o #gdb-low.o 
