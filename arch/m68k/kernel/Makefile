#
# Makefile for the linux kernel.
#

O_TARGET := kernel.o

ifndef CONFIG_SUN3
  EXTRA_TARGETS := head.o
else
  EXTRA_TARGETS := sun3-head.o
endif 

export-objs	:= setup.o m68k_ksyms.o

obj-y		:= entry.o process.o traps.o ints.o signal.o ptrace.o \
			sys_m68k.o time.o semaphore.o setup.o m68k_ksyms.o

obj-$(CONFIG_PCI)	+= bios32.o

EXTRA_AFLAGS := -traditional

include $(TOPDIR)/Rules.make

head.o: head.S m68k_defs.h

entry.o: entry.S m68k_defs.h

sun3-head.o: sun3-head.S m68k_defs.h

m68k_defs.h: m68k_defs.c m68k_defs.head
	rm -f m68k_defs.d
	SUNPRO_DEPENDENCIES="m68k_defs.d m68k_defs.h" \
	$(CC) $(filter-out -MD,$(CFLAGS)) -S m68k_defs.c
	cp m68k_defs.head m68k_defs.h
	grep '^#define' m68k_defs.s >> m68k_defs.h
	rm m68k_defs.s
-include m68k_defs.d

