#
# Makefile for the linux kernel.
#

EXTRA_TARGETS := kernel.o head.o init_task.o

O_TARGET := kernel.o

export-objs     := mca.o mtrr.o msr.o cpuid.o microcode.o i386_ksyms.o time.o

obj-y	:= process.o semaphore.o signal.o entry.o traps.o irq.o vm86.o \
		ptrace.o i8259.o ioport.o ldt.o setup.o time.o sys_i386.o \
		pci-dma.o i386_ksyms.o i387.o bluesmoke.o dmi_scan.o \
		bootflag.o

obj-y				+= cpu/
obj-$(CONFIG_MCA)		+= mca.o
obj-$(CONFIG_EISA)		+= eisa.o
obj-$(CONFIG_MTRR)		+= mtrr.o
obj-$(CONFIG_X86_MSR)		+= msr.o
obj-$(CONFIG_X86_CPUID)		+= cpuid.o
obj-$(CONFIG_MICROCODE)		+= microcode.o
obj-$(CONFIG_APM)		+= apm.o
obj-$(CONFIG_ACPI)		+= acpi.o
obj-$(CONFIG_ACPI_SLEEP)	+= acpi_wakeup.o
obj-$(CONFIG_SMP)		+= smp.o smpboot.o trampoline.o
obj-$(CONFIG_X86_LOCAL_APIC)	+= mpparse.o apic.o nmi.o
obj-$(CONFIG_X86_IO_APIC)	+= io_apic.o
obj-$(CONFIG_SOFTWARE_SUSPEND)	+= suspend.o
ifdef CONFIG_VISWS
obj-y += setup-visws.o
obj-$(CONFIG_X86_VISWS_APIC)	+= visws_apic.o
endif

EXTRA_AFLAGS   := -traditional

include $(TOPDIR)/Rules.make
