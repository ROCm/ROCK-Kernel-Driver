#
# Makefile for the linux kernel.
#

ifndef CONFIG_SUN3
  extra-y := head.o
else
  extra-y := sun3-head.o
endif
extra-y	+= vmlinux.lds.s

obj-y		:= entry.o process.o traps.o ints.o signal.o ptrace.o \
			sys_m68k.o time.o semaphore.o setup.o m68k_ksyms.o

obj-$(CONFIG_PCI)	+= bios32.o
obj-$(CONFIG_MODULES)	+= module.o

EXTRA_AFLAGS := -traditional

$(obj)/head.o: $(obj)/head.S $(obj)/m68k_defs.h

$(obj)/entry.o: $(obj)/entry.S $(obj)/m68k_defs.h

$(obj)/sun3-head.o: $(obj)/sun3-head.S $(obj)/m68k_defs.h

$(obj)/m68k_defs.h: $(src)/m68k_defs.c $(src)/m68k_defs.head
	rm -f $(obj)/m68k_defs.d
	SUNPRO_DEPENDENCIES="$(obj)/m68k_defs.d $(obj)/m68k_defs.h" \
	$(CC) $(filter-out -MD,$(CFLAGS)) -S $(src)/m68k_defs.c -o \
	$(obj)/m68k_defs.s
	cp $(src)/m68k_defs.head $(obj)/m68k_defs.h
	grep '^#define' $(obj)/m68k_defs.s >> $(obj)/m68k_defs.h
	rm $(obj)/m68k_defs.s
-include $(obj)/m68k_defs.d

