#
# Makefile for the kernel tracing drivers.
#

# Multipart objects.

ltt_driver-objs	:= ltt-core.o ltt-core_hooks.o


# Optional parts of multipart objects.
ifeq ($(CONFIG_X86),y)
ltt_driver-objs	+= i386_syscall.o
endif

obj-$(CONFIG_LTT)	+= ltt_driver.o

