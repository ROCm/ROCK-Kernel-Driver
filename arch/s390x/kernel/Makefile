#
# Makefile for the linux kernel.
#

EXTRA_TARGETS	:= head.o init_task.o
EXTRA_AFLAGS	:= -traditional

export-objs	:= debug.o ebcdic.o s390_ext.o smp.o s390_ksyms.o \
		   exec32.o

obj-y		:= entry.o bitmap.o traps.o time.o process.o \
		   setup.o sys_s390.o ptrace.o signal.o cpcmd.o ebcdic.o \
		   semaphore.o reipl.o s390_ext.o debug.o

obj-$(CONFIG_MODULES)		+= s390_ksyms.o
obj-$(CONFIG_SMP)		+= smp.o

#
# Kernel debugging
#
obj-$(CONFIG_REMOTE_DEBUG)	+= gdb-stub.o #gdb-low.o 

obj-$(CONFIG_S390_SUPPORT)	+= linux32.o signal32.o ioctl32.o wrapper32.o \
					 exec32.o exec_domain32.o
obj-$(CONFIG_BINFMT_ELF32)	+= binfmt_elf32.o

include $(TOPDIR)/Rules.make

#
# This is just to get the dependencies...
#
binfmt_elf32.o:	$(TOPDIR)/fs/binfmt_elf.c
