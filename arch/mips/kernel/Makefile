#
# Makefile for the Linux/MIPS kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) $(AFLAGS) $< -o $@
.S.o:
	$(CC) $(AFLAGS) -c $< -o $@

EXTRA_AFLAGS = -mips3 -mcpu=r4000

all:	kernel.o head.o init_task.o

O_TARGET := kernel.o

obj-y				+= branch.o process.o signal.o entry.o \
				   traps.o ptrace.o vm86.o ioport.o reset.o \
				   semaphore.o setup.o syscall.o sysmips.o \
				   ipc.o scall_o32.o unaligned.o
obj-$(CONFIG_MODULES)		+= mips_ksyms.o

ifdef CONFIG_CPU_R3000
obj-y	+= r2300_misc.o r2300_fpu.o r2300_switch.o
else
obj-y	+= r4k_misc.o r4k_switch.o 
ifdef CONFIG_CPU_R6000
obj-y	+= r6000_fpu.o
else
obj-y	+= r4k_fpu.o
endif
endif

obj-$(CONFIG_SMP)		+= smp.o

# Old style irq support, going to die in 2.5.
export-objs			+= old-irq.o
obj-$(CONFIG_NEW_IRQ)		+= irq.o
obj-$(CONFIG_ROTTEN_IRQ)	+= old-irq.o
obj-$(CONFIG_I8259)		+= i8259.o

# transition from old time.c to new time.c
# some boards uses old-time.c, some use time.c, and some use their own ones
export-objs			+= old-time.o time.o
obj-$(CONFIG_OLD_TIME_C)	+= old-time.o
obj-$(CONFIG_NEW_TIME_C)	+= time.o

obj-$(CONFIG_BINFMT_IRIX)	+= irixelf.o irixioctl.o irixsig.o sysirix.o \
				   irixinv.o
obj-$(CONFIG_REMOTE_DEBUG)	+= gdb-low.o gdb-stub.o 
obj-$(CONFIG_PCI)		+= pci-dma.o
obj-$(CONFIG_PROC_FS)		+= proc.o

ifdef CONFIG_PCI
obj-$(CONFIG_NEW_PCI)          += pci.o
obj-$(CONFIG_PCI_AUTO)         += pci_auto.o
endif

entry.o: entry.S
head.o: head.S

include $(TOPDIR)/Rules.make
