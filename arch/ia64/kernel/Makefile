#
# Makefile for the linux kernel.
#

.S.s:
	$(CPP) $(AFLAGS) $(AFLAGS_KERNEL) -o $*.s $<
.S.o:
	$(CC) $(AFLAGS) $(AFLAGS_KERNEL) -c -o $*.o $<

all: kernel.o head.o init_task.o

O_TARGET := kernel.o

obj-y := acpi.o entry.o gate.o efi.o efi_stub.o irq.o irq_ia64.o irq_sapic.o ivt.o \
	 machvec.o pal.o process.o perfmon.o ptrace.o sal.o semaphore.o setup.o	\
	 signal.o sys_ia64.o traps.o time.o unaligned.o unwind.o
obj-$(CONFIG_IA64_GENERIC) += machvec.o iosapic.o
obj-$(CONFIG_IA64_DIG) += iosapic.o
obj-$(CONFIG_IA64_PALINFO) += palinfo.o
obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_SMP) += smp.o smpboot.o
obj-$(CONFIG_IA64_MCA) += mca.o mca_asm.o
obj-$(CONFIG_IA64_BRL_EMU) += brl_emu.o

export-objs := ia64_ksyms.o

clean::

include $(TOPDIR)/Rules.make
