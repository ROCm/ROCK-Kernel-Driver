#
# Makefile for the Linux/SuperH kernel.
#

extra-y	:= head.o init_task.o vmlinux.lds.s

obj-y	:= process.o signal.o entry.o traps.o irq.o \
	ptrace.o setup.o time.o sys_sh.o semaphore.o \
	io.o io_generic.o sh_ksyms.o

obj-y				+= cpu/

obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_CF_ENABLER)	+= cf-enabler.o
obj-$(CONFIG_SH_STANDARD_BIOS)	+= sh_bios.o
obj-$(CONFIG_SH_KGDB)		+= kgdb_stub.o kgdb_jmp.o
obj-$(CONFIG_SH_CPU_FREQ)	+= cpufreq.o
obj-$(CONFIG_MODULES)		+= module.o

ifneq ($(CONFIG_SH_DREAMCAST),y)
obj-$(CONFIG_PCI)		+= pci-dma.o
endif
obj-$(CONFIG_PCI)		+= pci.o
obj-$(CONFIG_PCI_AUTO)		+= pci_auto.o

USE_STANDARD_AS_RULE := true

