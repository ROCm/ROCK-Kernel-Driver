#
# Makefile for the Linux/MIPS kernel.
#

EXTRA_TARGETS	:= head.o init_task.o

export-objs	:= mips64_ksyms.o

obj-y	:= branch.o entry.o proc.o process.o ptrace.o r4k_cache.o r4k_fpu.o \
	   r4k_genex.o r4k_switch.o r4k_tlb.o r4k_tlb_debug.o r4k_tlb_glue.o \
	   scall_64.o semaphore.o setup.o signal.o softfp.o syscall.o \
	   traps.o unaligned.o

obj-$(CONFIG_MODULES)		+= mips64_ksyms.o
obj-$(CONFIG_MIPS32_COMPAT)	+= linux32.o scall_o32.o signal32.o ioctl32.o
obj-$(CONFIG_BINFMT_ELF32)	+= binfmt_elf32.o
obj-$(CONFIG_SMP)		+= smp.o

AFLAGS_r4k_genex.o := -P
AFLAGS_r4k_tlb_glue.o := -P
EXTRA_AFLAGS := $(CFLAGS)

include $(TOPDIR)/Rules.make
