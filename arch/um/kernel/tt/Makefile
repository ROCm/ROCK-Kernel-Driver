# 
# Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

extra-y := unmap_fin.o

obj-y = exec_kern.o exec_user.o gdb.o ksyms.o mem.o mem_user.o process_kern.o \
	syscall_kern.o syscall_user.o time.o tlb.o tracer.o trap_user.o \
	uaccess_user.o sys-$(SUBARCH)/

obj-$(CONFIG_PT_PROXY) += gdb_kern.o ptproxy/

USER_OBJS := $(filter %_user.o,$(obj-y)) gdb.o time.o tracer.o
USER_OBJS := $(foreach file,$(USER_OBJS),$(obj)/$(file))

UNMAP_CFLAGS := $(patsubst -pg -DPROFILING,,$(USER_CFLAGS))
UNMAP_CFLAGS := $(patsubst -fprofile-arcs -ftest-coverage,,$(UNMAP_CFLAGS))

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$(notdir $@)) $(USER_CFLAGS) -c -o $@ $<

$(O_TARGET) : $(obj)/unmap_fin.o

$(obj)/unmap.o: $(src)/unmap.c
	$(CC) $(UNMAP_CFLAGS) -c -o $@ $<

$(obj)/unmap_fin.o : $(src)/unmap.o
	ld -r -o $@ $< -lc -L/usr/lib

clean :
