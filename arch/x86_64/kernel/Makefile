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

all: kernel.o head.o head64.o init_task.o

O_TARGET := kernel.o


export-objs     := mtrr.o msr.o cpuid.o x8664_ksyms.o

obj-y	:= process.o semaphore.o signal.o entry.o traps.o irq.o \
		ptrace.o i8259.o ioport.o ldt.o setup.o time.o sys_x86_64.o \
		pci-dma.o x8664_ksyms.o i387.o syscall.o early_printk.o vsyscall.o \
		setup64.o bluesmoke.o

ifdef CONFIG_PCI
obj-y			+= pci-x86_64.o
obj-y			+= pci-pc.o pci-irq.o
endif

obj-$(CONFIG_MTRR)	+= mtrr.o
obj-$(CONFIG_X86_MSR)	+= msr.o
obj-$(CONFIG_X86_CPUID)	+= cpuid.o
obj-$(CONFIG_SMP)	+= smp.o smpboot.o trampoline.o
obj-$(CONFIG_X86_LOCAL_APIC)	+= apic.o  nmi.o
obj-$(CONFIG_X86_IO_APIC)	+= io_apic.o mpparse.o

include $(TOPDIR)/Rules.make

