#
# Makefile for IBM Dynamic Probes
#

# Multipart objects.
dp-objs		:= dprobes.o dprobes_in.o

# Optional parts of multipart objects.
ifeq ($(CONFIG_X86),y)
	dp-objs += i386/dprobes.o
endif

obj-$(CONFIG_DPROBES)	+= dp.o
