#
# Makefile for the linux kernel.
#

extra-y	:= head.o init_task.o

obj-y := acpi.o entry.o efi.o efi_stub.o gate-data.o fsys.o ia64_ksyms.o irq.o irq_ia64.o	\
	 irq_lsapic.o ivt.o machvec.o pal.o patch.o process.o perfmon.o ptrace.o sal.o		\
	 semaphore.o setup.o signal.o sys_ia64.o time.o traps.o unaligned.o unwind.o

obj-$(CONFIG_EFI_VARS)		+= efivars.o
obj-$(CONFIG_IA64_BRL_EMU)	+= brl_emu.o
obj-$(CONFIG_IA64_GENERIC)	+= acpi-ext.o
obj-$(CONFIG_IA64_HP_ZX1)	+= acpi-ext.o
obj-$(CONFIG_IA64_MCA)		+= mca.o mca_asm.o
obj-$(CONFIG_IA64_PALINFO)	+= palinfo.o
obj-$(CONFIG_IOSAPIC)		+= iosapic.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_SMP)		+= smp.o smpboot.o
obj-$(CONFIG_PERFMON)		+= perfmon_default_smpl.o

AFLAGS_gate.lds.o += -P -C -U$(ARCH)
arch/ia64/kernel/gate.lds.s: %.s: %.S scripts FORCE
	$(call if_changed_dep,as_s_S)
$(obj)/gate.so: $(src)/gate.lds.s $(obj)/gate.o
	$(CC) -nostdlib -shared -s -Wl,-soname=linux-gate.so.1 \
		-o $@ -Wl,-T,$^
extra-y += gate.so

$(obj)/built-in.o: $(obj)/gate-syms.o
$(obj)/built-in.o: ld_flags += -R $(obj)/gate-syms.o
$(obj)/gate-syms.o: $(src)/gate.lds.s $(obj)/gate.o
	$(CC) -nostdlib -r -o $@ -Wl,-T,$^
extra-y += gate-syms.o

$(obj)/gate-data.o: $(obj)/gate.so
