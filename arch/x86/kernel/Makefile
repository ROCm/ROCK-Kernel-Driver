ifeq ($(CONFIG_X86_32),y)
include ${srctree}/arch/x86/kernel/Makefile_32
else
include ${srctree}/arch/x86/kernel/Makefile_64
endif

# Workaround to delete .lds files with make clean
# The problem is that we do not enter Makefile_32 with make clean.
clean-files := vsyscall*.lds vsyscall*.so

disabled-obj-$(CONFIG_XEN) := early-quirks.o genapic_flat_$(BITS).o hpet.o i8253.o i8259_$(BITS).o \
	reboot_$(BITS).o smpboot_$(BITS).o suspend_$(BITS).o trampoline_$(BITS).o tsc_$(BITS).o tsc_sync.o
