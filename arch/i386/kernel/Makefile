#
# Makefile for the linux kernel.
#

extra-y := head.o init_task.o

obj-y	:= process.o semaphore.o signal.o entry.o traps.o irq.o vm86.o \
		ptrace.o i8259.o ioport.o ldt.o setup.o time.o sys_i386.o \
		pci-dma.o i386_ksyms.o i387.o dmi_scan.o bootflag.o \
		doublefault.o

obj-y				+= cpu/
obj-y				+= timers/
obj-$(CONFIG_ACPI)		+= acpi/
obj-$(CONFIG_X86_BIOS_REBOOT)	+= reboot.o
obj-$(CONFIG_MCA)		+= mca.o
obj-$(CONFIG_X86_MSR)		+= msr.o
obj-$(CONFIG_X86_CPUID)		+= cpuid.o
obj-$(CONFIG_MICROCODE)		+= microcode.o
obj-$(CONFIG_APM)		+= apm.o
obj-$(CONFIG_X86_SMP)		+= smp.o smpboot.o
obj-$(CONFIG_X86_TRAMPOLINE)	+= trampoline.o
obj-$(CONFIG_X86_MPPARSE)	+= mpparse.o
obj-$(CONFIG_X86_LOCAL_APIC)	+= apic.o nmi.o
obj-$(CONFIG_X86_IO_APIC)	+= io_apic.o
obj-$(CONFIG_SOFTWARE_SUSPEND)	+= suspend.o suspend_asm.o
obj-$(CONFIG_X86_NUMAQ)		+= numaq.o
obj-$(CONFIG_EDD)             	+= edd.o
obj-$(CONFIG_MODULES)		+= module.o
obj-y				+= sysenter.o vsyscall.o
obj-$(CONFIG_ACPI_SRAT) 	+= srat.o

EXTRA_AFLAGS   := -traditional

obj-$(CONFIG_SCx200)		+= scx200.o

# vsyscall.o contains the vsyscall DSO images as __initdata.
# We must build both images before we can assemble it.
$(obj)/vsyscall.o: $(obj)/vsyscall-int80.so $(obj)/vsyscall-sysenter.so
extra-y += $(foreach F,int80 sysenter,vsyscall-$F.o vsyscall-$F.so)

# The DSO images are built using a special linker script.
$(obj)/vsyscall-int80.so $(obj)/vsyscall-sysenter.so: \
$(obj)/vsyscall-%.so: $(src)/vsyscall.lds $(obj)/vsyscall-%.o
	$(CC) -nostdlib -shared -s -Wl,-soname=linux-vsyscall.so.1 \
	      -o $@ -Wl,-T,$^

# We also create a special relocatable object that should mirror the symbol
# table and layout of the linked DSO.  With ld -R we can then refer to
# these symbols in the kernel code rather than hand-coded addresses.
extra-y += vsyscall-syms.o
$(obj)/built-in.o: $(obj)/vsyscall-syms.o
$(obj)/built-in.o: ld_flags += -R $(obj)/vsyscall-syms.o
$(obj)/vsyscall-syms.o: $(src)/vsyscall.lds $(obj)/vsyscall-sysenter.o
	$(CC) -nostdlib -r -o $@ -Wl,-T,$^
