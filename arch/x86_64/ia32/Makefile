#
# Makefile for the ia32 kernel emulation subsystem.
#

obj-$(CONFIG_IA32_EMULATION) := ia32entry.o sys_ia32.o ia32_ioctl.o \
	ia32_signal.o tls32.o \
	ia32_binfmt.o fpu32.o ptrace32.o ipc32.o syscall32.o

$(obj)/syscall32.o: $(src)/syscall32.c $(obj)/vsyscall.so

# Teach kbuild about targets
targets := vsyscall.o vsyscall.so

# The DSO images are built using a special linker script
quiet_cmd_vsyscall = SYSCALL $@
      cmd_vsyscall = $(CC) -m32 -nostdlib -shared -s \
			   -Wl,-soname=linux-gate.so.1 -o $@ \
			   -Wl,-T,$(filter-out FORCE,$^)
$(obj)/vsyscall.so: $(src)/vsyscall.lds $(obj)/vsyscall.o FORCE
	$(call if_changed,vsyscall)

AFLAGS_vsyscall.o = -m32
CFLAGS_ia32_ioctl.o += -Ifs/
