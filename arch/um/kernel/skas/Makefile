# 
# Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

obj-y := exec_kern.o exec_user.o mem.o mem_user.o mmu.o process.o \
	process_kern.o syscall_kern.o syscall_user.o time.o tlb.o trap_user.o \
	uaccess.o sys-$(SUBARCH)/

subdir-y := util

USER_OBJS = $(filter %_user.o,$(obj-y)) process.o time.o
USER_OBJS := $(foreach file,$(USER_OBJS),$(obj)/$(file))

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$(notdir $@)) $(USER_CFLAGS) -c -o $@ $<
