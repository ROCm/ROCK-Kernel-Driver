#
# Makefile for the kernel tracing drivers.
#

# Multipart objects.

ltt_driver-objs	:= ltt-core.o ltt-core_hooks.o


# Optional parts of multipart objects.
ifeq ($(CONFIG_ARM),y)
ltt_driver-objs	+= arm_syscall.o
endif
ifeq ($(CONFIG_X86),y)
ltt_driver-objs	+= i386_syscall.o
endif
ifeq ($(CONFIG_MIPS32),y)
ltt_driver-objs	+= mips_syscall.o
endif
ifeq ($(CONFIG_PPC32),y)
ltt_driver-objs	+= ppc_syscall.o
endif
ifeq ($(CONFIG_ARCH_S390),y)
ltt_driver-objs	+= s390_syscall.o
endif
ifeq ($(CONFIG_SUPERH),y)
ltt_driver-objs	+= sh_syscall.o
endif

obj-$(CONFIG_LTT)	+= ltt_driver.o

