#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.o:
	$(CC) $(AFLAGS) -traditional -c $< -o $*.o

all: kernel.o head.o init_task.o

O_TARGET := kernel.o
O_OBJS   := lowcore.o entry.o bitmap.o traps.o time.o process.o irq.o \
            setup.o sys_s390.o ptrace.o signal.o cpcmd.o ebcdic.o \
            semaphore.o s390fpu.o s390io.o s390mach.o s390dyn.o reipl.o
OX_OBJS := s390_ksyms.o
MX_OBJS  :=

ifdef CONFIG_SMP
O_OBJS += smp.o
endif

ifdef CONFIG_PCI
O_OBJS += bios32.o
endif

ifdef CONFIG_MCA
O_OBJS += mca.o
endif

ifeq ($(CONFIG_MTRR),y)
OX_OBJS += mtrr.o
else
  ifeq ($(CONFIG_MTRR),m)
  MX_OBJS += mtrr.o
  endif
endif

ifeq ($(CONFIG_IEEEFPU_EMULATION),y)
  O_OBJS += mathemu.o floatlib.o
endif

#
# Kernel debugging
#
ifdef CONFIG_REMOTE_DEBUG
O_OBJS += gdb-stub.o #gdb-low.o 
endif

include $(TOPDIR)/Rules.make

