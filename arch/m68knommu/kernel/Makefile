#
# Makefile for arch/m68knommu/kernel.
#

extra-y := vmlinux.lds.s

obj-y += entry.o init_task.o m68k_ksyms.o process.o ptrace.o \
	 semaphore.o setup.o signal.o syscalltable.o sys_m68k.o time.o \
	 traps.o

obj-$(CONFIG_COMEMPCI)	+= comempci.o
