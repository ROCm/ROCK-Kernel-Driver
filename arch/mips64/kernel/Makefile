#
# Makefile for the Linux/MIPS kernel.
#

extra-y		:= head.o init_task.o

obj-y		:= branch.o cpu-probe.o entry.o irq.o proc.o process.o \
		   ptrace.o r4k_cache.o r4k_fpu.o r4k_genex.o r4k_switch.o \
		   reset.o scall_64.o semaphore.o setup.o signal.o syscall.o \
		   time.o traps.o unaligned.o

obj-$(CONFIG_I8259)		+= i8259.o
obj-$(CONFIG_IRQ_CPU)		+= irq_cpu.o

obj-$(CONFIG_MODULES)		+= mips64_ksyms.o
obj-$(CONFIG_MIPS32_COMPAT)	+= linux32.o signal32.o ioctl32.o
obj-$(CONFIG_MIPS32_N32)	+= binfmt_elfn32.o scall_n32.o
obj-$(CONFIG_MIPS32_O32)	+= binfmt_elfo32.o scall_o32.o
obj-$(CONFIG_SMP)		+= smp.o

ifndef CONFIG_MAPPED_PCI_IO
obj-y				+= pci-dma.o
endif

obj-$(CONFIG_MODULES)		+= module.o

CFLAGS_cpu-probe.o	= $(shell if $(CC) $(CFLAGS) -Wa,-mdaddi -c -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-DHAVE_AS_SET_DADDI"; fi)
AFLAGS_r4k_genex.o	= -P

EXTRA_AFLAGS := $(CFLAGS)
