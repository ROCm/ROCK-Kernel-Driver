# $Id: Makefile,v 1.70 2002/02/09 19:49:30 davem Exp $
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

SH = $(CONFIG_SHELL)

.S.s:
	$(CPP) $(AFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) $(AFLAGS) -ansi -c $< -o $*.o

all: kernel.o head.o init_task.o

O_TARGET := kernel.o

export-objs	:= sparc64_ksyms.o
obj-y		:= process.o setup.o cpu.o idprom.o \
		   traps.o devices.o auxio.o \
		   irq.o ptrace.o time.o sys_sparc.o signal.o \
		   unaligned.o central.o pci.o starfire.o semaphore.o \
		   power.o sbus.o iommu_common.o sparc64_ksyms.o chmc.o

obj-$(CONFIG_PCI)	 += ebus.o isa.o pci_common.o pci_iommu.o \
			    pci_psycho.o pci_sabre.o pci_schizo.o
obj-$(CONFIG_SMP)	 += smp.o trampoline.o
obj-$(CONFIG_SPARC32_COMPAT) += sys32.o sys_sparc32.o signal32.o ioctl32.o
obj-$(CONFIG_BINFMT_ELF32) += binfmt_elf32.o
obj-$(CONFIG_BINFMT_AOUT32) += binfmt_aout32.o

ifdef CONFIG_SUNOS_EMUL
  obj-y += sys_sunos32.o sunos_ioctl32.o
else
  ifdef CONFIG_SOLARIS_EMUL
    obj-y += sys_sunos32.o sunos_ioctl32.o
  endif
endif

head.o: head.S ttable.S itlb_base.S dtlb_base.S dtlb_backend.S dtlb_prot.S \
	etrap.S rtrap.S winfixup.S entry.S
	$(CC) $(AFLAGS) -ansi -c $*.S -o $*.o

#
# This is just to get the dependencies...
#
binfmt_elf32.o: $(TOPDIR)/fs/binfmt_elf.c

ifneq ($(NEW_GCC),y)
  CMODEL_CFLAG := -mmedlow
else
  CMODEL_CFLAG := -m64 -mcmodel=medlow
endif

include $(TOPDIR)/Rules.make
