#
# Makefile for the linux kernel.
#

O_TARGET	:= kernel.o
EXTRA_TARGETS 	:= head.o head64.o init_task.o

export-objs     := mtrr.o msr.o cpuid.o x8664_ksyms.o

obj-y	:= process.o semaphore.o signal.o entry.o traps.o irq.o \
		ptrace.o i8259.o ioport.o ldt.o setup.o time.o sys_x86_64.o \
		pci-dma.o x8664_ksyms.o i387.o syscall.o vsyscall.o \
		setup64.o bluesmoke.o bootflag.o

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
#obj-$(CONFIG_ACPI)             += acpi.o
#obj-$(CONFIG_ACPI_SLEEP)       += acpi_wakeup.o
obj-$(CONFIG_EARLY_PRINTK)    += early_printk.o

EXTRA_AFLAGS := -traditional

include $(TOPDIR)/Rules.make

