#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o

all: kernel.o head.o init_task.o

O_TARGET	:= kernel.o

export-objs	:= debug.o ebcdic.o irq.o s390_ext.o smp.o s390_ksyms.o \
		   exec32.o

obj-y		:= entry.o bitmap.o traps.o time.o process.o irq.o \
		   setup.o sys_s390.o ptrace.o signal.o cpcmd.o ebcdic.o \
		   semaphore.o s390fpu.o reipl.o s390_ext.o debug.o

obj-$(CONFIG_MODULES)		+= s390_ksyms.o
obj-$(CONFIG_SMP)		+= smp.o

#
# Kernel debugging
#
obj-$(CONFIG_REMOTE_DEBUG)	+= gdb-stub.o #gdb-low.o 

obj-$(CONFIG_S390_SUPPORT)	+= linux32.o signal32.o ioctl32.o wrapper32.o exec32.o
obj-$(CONFIG_BINFMT_ELF32)	+= binfmt_elf32.o

#
# This is just to get the dependencies...
#
binfmt_elf32.o:	$(TOPDIR)/fs/binfmt_elf.c

include $(TOPDIR)/Rules.make
