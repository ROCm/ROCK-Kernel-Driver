#
# Makefile for arch/parisc/kernel
#

ifdef CONFIG_PARISC64
EXTRA_TARGETS := init_task.o pdc_cons.o process.o head64.o unaligned.o perf.o perf_asm.o
else
EXTRA_TARGETS := init_task.o pdc_cons.o process.o head.o unaligned.o
endif

AFLAGS_entry.o := -traditional
AFLAGS_pacache.o := -traditional

export-objs	:= parisc_ksyms.o 

obj-y	     	:= cache.o pacache.o setup.o traps.o time.o irq.o \
		   pa7300lc.o syscall.o entry.o sys_parisc.o firmware.o \
		   ptrace.o hardware.o inventory.o drivers.o semaphore.o \
		   signal.o hpmc.o real2.o parisc_ksyms.o unaligned.o \
		   processor.o pdc_chassis.o

obj-$(CONFIG_SMP) += smp.o 
obj-$(CONFIG_PA11) += pci-dma.o
obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_PARISC64) += binfmt_elf32.o sys_parisc32.o \
		ioctl32.o signal32.o
# only supported for PCX-W/U in 64-bit mode at the moment
obj-$(CONFIG_PARISC64) += perf.o perf_asm.o
