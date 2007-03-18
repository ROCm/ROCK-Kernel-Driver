#
# Makefile for the linux kernel.
#

extra-y := head.o init_task.o vmlinux.lds

obj-y	:= process.o signal.o entry.o traps.o irq.o \
		ptrace.o time.o ioport.o ldt.o setup.o i8259.o sys_i386.o \
		pci-dma.o i386_ksyms.o i387.o bootflag.o e820.o\
		quirks.o i8237.o topology.o alternative.o i8253.o tsc.o

obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-y				+= cpu/
obj-y				+= acpi/
obj-$(CONFIG_X86_BIOS_REBOOT)	+= reboot.o
obj-$(CONFIG_MCA)		+= mca.o
obj-$(CONFIG_X86_MSR)		+= msr.o
obj-$(CONFIG_X86_CPUID)		+= cpuid.o
obj-$(CONFIG_MICROCODE)		+= microcode.o
obj-$(CONFIG_APM)		+= apm.o
obj-$(CONFIG_X86_SMP)		+= smp.o smpboot.o tsc_sync.o
obj-$(CONFIG_X86_TRAMPOLINE)	+= trampoline.o
obj-$(CONFIG_X86_MPPARSE)	+= mpparse.o
obj-$(CONFIG_X86_LOCAL_APIC)	+= apic.o nmi.o
obj-$(CONFIG_X86_IO_APIC)	+= io_apic.o
obj-$(CONFIG_X86_REBOOTFIXUPS)	+= reboot_fixups.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o crash.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_X86_NUMAQ)		+= numaq.o
obj-$(CONFIG_X86_SUMMIT_NUMA)	+= summit.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_MODULES)		+= module.o
obj-y				+= sysenter.o vsyscall.o
obj-$(CONFIG_ACPI_SRAT) 	+= srat.o
obj-$(CONFIG_EFI) 		+= efi.o efi_stub.o
obj-$(CONFIG_DOUBLEFAULT) 	+= doublefault.o
obj-$(CONFIG_VM86)		+= vm86.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_HPET_TIMER) 	+= hpet.o
obj-$(CONFIG_K8_NB)		+= k8.o

obj-$(CONFIG_VMI)		+= vmi.o vmitime.o
obj-$(CONFIG_PARAVIRT)		+= paravirt.o
obj-y				+= pcspeaker.o

EXTRA_AFLAGS   := -traditional

obj-$(CONFIG_SCx200)		+= scx200.o

ifdef CONFIG_XEN
vsyscall_note := vsyscall-note-xen.o
else
vsyscall_note := vsyscall-note.o
endif

# vsyscall.o contains the vsyscall DSO images as __initdata.
# We must build both images before we can assemble it.
# Note: kbuild does not track this dependency due to usage of .incbin
$(obj)/vsyscall.o: $(obj)/vsyscall-int80.so $(obj)/vsyscall-sysenter.so
targets += $(foreach F,int80 sysenter,vsyscall-$F.o vsyscall-$F.so)
targets += $(vsyscall_note) vsyscall.lds

# The DSO images are built using a special linker script.
quiet_cmd_syscall = SYSCALL $@
      cmd_syscall = $(CC) -m elf_i386 -nostdlib $(SYSCFLAGS_$(@F)) \
		          -Wl,-T,$(filter-out FORCE,$^) -o $@

export CPPFLAGS_vsyscall.lds += -P -C -U$(ARCH)

vsyscall-flags = -shared -s -Wl,-soname=linux-gate.so.1 \
		 $(call ld-option, -Wl$(comma)--hash-style=sysv)
SYSCFLAGS_vsyscall-sysenter.so	= $(vsyscall-flags)
SYSCFLAGS_vsyscall-int80.so	= $(vsyscall-flags)

$(obj)/vsyscall-int80.so $(obj)/vsyscall-sysenter.so: \
$(obj)/vsyscall-%.so: $(src)/vsyscall.lds \
		      $(obj)/vsyscall-%.o $(obj)/$(vsyscall_note) FORCE
	$(call if_changed,syscall)

ifeq ($(CONFIG_XEN)$(CONFIG_COMPAT_VDSO),yy)

# vsyscall.o also contains the vsyscall DSO relocation info as __initdata.
# We must build both the alternative images before we can assemble it.
# Note: kbuild does not track this dependency due to usage of .include
$(obj)/vsyscall.o: $(obj)/vsyscall-int80.rel $(obj)/vsyscall-sysenter.rel
targets += $(foreach F,int80 sysenter,vsyscall-$F.so.alt vsyscall-$F.rel)
targets += vsyscall.lds.alt

# The alternative DSO images are built using an alternate base address.
quiet_cmd_syscall_alt = REBASE  $@
      cmd_syscall_alt = sed 's,^\([[:space:]]*\.[[:space:]]*=[[:space:]]*\),\1-0x55AA0000 + ,' $< >$@

quiet_cmd_syscall_rel = COMPARE $@
      cmd_syscall_rel = set -e; \
			cmp -l $(basename $<) $< \
			| { read off1 old1 new1; \
			    read off2 old2 new2; \
			    test $$(expr $$off1 + 1) = $$off2; \
			    echo " .long $$(expr $$off1 - 3)"; \
			    while read off1 old3 new3; do \
				read off2 old4 new4; \
				test $$(expr $$off1 + 1) = $$off2; \
				test $$old1 = $$old3 -a $$new1 = $$new3; \
				test $$old2 = $$old4 -a $$new2 = $$new4; \
				echo " .long $$(expr $$off1 - 3)"; \
			    done; \
			  } >$@

SYSCFLAGS_vsyscall-sysenter.so.alt = $(vsyscall-flags)
SYSCFLAGS_vsyscall-int80.so.alt    = $(vsyscall-flags)

$(obj)/vsyscall.lds.alt: $(obj)/vsyscall.lds FORCE
	$(call if_changed,syscall_alt)

$(obj)/vsyscall-int80.so.alt $(obj)/vsyscall-sysenter.so.alt: \
$(obj)/vsyscall-%.so.alt: $(obj)/vsyscall.lds.alt \
		      $(obj)/vsyscall-%.o $(obj)/$(vsyscall_note) FORCE
	$(call if_changed,syscall)

$(obj)/vsyscall-int80.rel $(obj)/vsyscall-sysenter.rel: \
$(obj)/vsyscall-%.rel: $(obj)/vsyscall-%.so.alt FORCE
	$(call if_changed,syscall_rel)

endif

# We also create a special relocatable object that should mirror the symbol
# table and layout of the linked DSO.  With ld -R we can then refer to
# these symbols in the kernel code rather than hand-coded addresses.
extra-y += vsyscall-syms.o
$(obj)/built-in.o: $(obj)/vsyscall-syms.o
$(obj)/built-in.o: ld_flags += -R $(obj)/vsyscall-syms.o

SYSCFLAGS_vsyscall-syms.o = -r
$(obj)/vsyscall-syms.o: $(src)/vsyscall.lds \
			$(obj)/vsyscall-sysenter.o $(obj)/$(vsyscall_note) FORCE
	$(call if_changed,syscall)

k8-y                      += ../../x86_64/kernel/k8.o
stacktrace-y		  += ../../x86_64/kernel/stacktrace.o

ifdef CONFIG_XEN
include $(srctree)/scripts/Makefile.xen

obj-y += fixup.o
microcode-$(subst m,y,$(CONFIG_MICROCODE)) := microcode-xen.o
n-obj-xen := i8259.o reboot.o smpboot.o trampoline.o tsc.o

obj-y := $(call filterxen, $(obj-y), $(n-obj-xen))
obj-y := $(call cherrypickxen, $(obj-y))
extra-y := $(call cherrypickxen, $(extra-y))
endif
