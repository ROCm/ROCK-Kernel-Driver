#
# Makefile for the Linux/MIPS kernel.
#

extra-y		:= head.o init_task.o

obj-y		+= branch.o cpu-probe.o process.o signal.o entry.o traps.o \
		   ptrace.o irq.o reset.o semaphore.o setup.o syscall.o \
		   sysmips.o ipc.o scall_o32.o time.o unaligned.o

obj-$(CONFIG_MODULES)		+= mips_ksyms.o

obj-$(CONFIG_CPU_R3000)		+= r2300_fpu.o r2300_switch.o
obj-$(CONFIG_CPU_TX39XX)	+= r2300_fpu.o r2300_switch.o
obj-$(CONFIG_CPU_TX49XX)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R4000)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_VR41XX)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R4300)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R4X00)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R5000)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R5432)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_RM7000)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_NEVADA)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R10000)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_SB1)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_MIPS32)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_MIPS64)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R6000)		+= r6000_fpu.o r4k_switch.o

obj-$(CONFIG_SMP)		+= smp.o

obj-$(CONFIG_I8259)		+= i8259.o
obj-$(CONFIG_IRQ_CPU)		+= irq_cpu.o

obj-$(CONFIG_BINFMT_IRIX)	+= irixelf.o irixioctl.o irixsig.o sysirix.o \
				   irixinv.o
obj-$(CONFIG_KGDB)		+= gdb-low.o gdb-stub.o
obj-$(CONFIG_PROC_FS)		+= proc.o

ifndef CONFIG_MAPPED_PCI_IO
obj-y				+= pci-dma.o
endif

obj-$(CONFIG_MODULES)		+= module.o

EXTRA_AFLAGS := $(CFLAGS)
