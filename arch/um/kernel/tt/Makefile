# 
# Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

obj-y = exec_kern.o exec_user.o gdb.o gdb_kern.o ksyms.o mem.o process_kern.o \
	syscall_kern.o syscall_user.o time.o tlb.o tracer.o trap_user.o \
	uaccess_user.o sys-$(SUBARCH)/

obj-$(CONFIG_PT_PROXY) += ptproxy/

USER_OBJS := $(filter %_user.o,$(obj-y)) gdb.o time.o tracer.o
USER_OBJS := $(foreach file,$(USER_OBJS),$(obj)/$(file))

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$(notdir $@)) $(USER_CFLAGS) -c -o $@ $<

clean :
