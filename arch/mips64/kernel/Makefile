#
# Makefile for the Linux/MIPS kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.o:
	$(CC) $(CFLAGS) $(CFLAGS_$@) -c $< -o $*.o

all:	kernel.o head.o init_task.o

O_TARGET := kernel.o

export-objs = mips64_ksyms.o

obj-y	:= branch.o entry.o proc.o process.o ptrace.o r4k_cache.o r4k_fpu.o \
	   r4k_genex.o r4k_switch.o r4k_tlb.o r4k_tlb_debug.o r4k_tlb_glue.o \
	   scall_64.o semaphore.o setup.o signal.o softfp.o syscall.o \
	   traps.o unaligned.o

obj-$(CONFIG_MODULES)		+= mips64_ksyms.o
obj-$(CONFIG_MIPS32_COMPAT)	+= linux32.o scall_o32.o signal32.o ioctl32.o
obj-$(CONFIG_BINFMT_ELF32)	+= binfmt_elf32.o
obj-$(CONFIG_SMP)		+= smp.o

CFLAGS_r4k_genex.o := -P
CFLAGS_r4k_tlb_glue.o := -P

include $(TOPDIR)/Rules.make
