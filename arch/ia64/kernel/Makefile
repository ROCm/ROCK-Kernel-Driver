#
# Makefile for the linux kernel.
#

extra-y	:= head.o init_task.o

obj-y := acpi.o entry.o efi.o efi_stub.o gate.o ia64_ksyms.o irq.o irq_ia64.o irq_lsapic.o	\
	 ivt.o machvec.o pal.o perfmon.o process.o ptrace.o sal.o semaphore.o setup.o signal.o	\
	 sys_ia64.o time.o traps.o unaligned.o unwind.o

obj-$(CONFIG_EFI_VARS)		+= efivars.o
obj-$(CONFIG_FSYS)		+= fsys.o
obj-$(CONFIG_IA64_BRL_EMU)	+= brl_emu.o
obj-$(CONFIG_IA64_GENERIC)	+= acpi-ext.o
obj-$(CONFIG_IA64_HP_ZX1)	+= acpi-ext.o
obj-$(CONFIG_IA64_MCA)		+= mca.o mca_asm.o
obj-$(CONFIG_IA64_PALINFO)	+= palinfo.o
obj-$(CONFIG_IOSAPIC)		+= iosapic.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_SMP)		+= smp.o smpboot.o

arch/$(ARCH)/kernel/asm-offsets.s: include/asm-$(ARCH)/.offsets.h.stamp

include/asm-$(ARCH)/.offsets.h.stamp:
	[ -s include/asm-$(ARCH)/offsets.h ] || echo "#define IA64_TASK_SIZE 0" > include/asm-$(ARCH)/offsets.h
	touch $@

CLEAN_FILES += include/asm-ia64/.offsets.h.stamp
