#
# Makefile for the ia32 kernel emulation subsystem.
#

obj-y := ia32_entry.o sys_ia32.o ia32_ioctl.o ia32_signal.o \
	 ia32_support.o ia32_traps.o binfmt_elf32.o ia32_ldt.o

CFLAGS_ia32_ioctl.o += -Ifs/
