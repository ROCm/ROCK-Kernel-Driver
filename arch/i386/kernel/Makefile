#
# Makefile for the linux kernel.
#

EXTRA_TARGETS := kernel.o head.o init_task.o

O_TARGET := kernel.o

export-objs     := mca.o i386_ksyms.o time.o

obj-y	:= process.o semaphore.o signal.o entry.o traps.o irq.o vm86.o \
		ptrace.o i8259.o ioport.o ldt.o setup.o time.o sys_i386.o \
		pci-dma.o i386_ksyms.o i387.o bluesmoke.o dmi_scan.o \
		bootflag.o

obj-y				+= cpu/
obj-$(CONFIG_X86_BIOS_REBOOT)	+= reboot.o
obj-$(CONFIG_MCA)		+= mca.o
obj-$(CONFIG_X86_MSR)		+= msr.o
obj-$(CONFIG_X86_CPUID)		+= cpuid.o
obj-$(CONFIG_MICROCODE)		+= microcode.o
obj-$(CONFIG_APM)		+= apm.o
obj-$(CONFIG_X86_SMP)		+= smp.o smpboot.o trampoline.o
obj-$(CONFIG_X86_MPPARSE)	+= mpparse.o
obj-$(CONFIG_X86_LOCAL_APIC)	+= apic.o nmi.o
obj-$(CONFIG_X86_IO_APIC)	+= io_apic.o
obj-$(CONFIG_SOFTWARE_SUSPEND)	+= suspend.o
obj-$(CONFIG_X86_NUMAQ)		+= numaq.o

EXTRA_AFLAGS   := -traditional

include $(TOPDIR)/Rules.make
