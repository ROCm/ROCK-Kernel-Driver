# 
# Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

obj-y = exec_kern.o exec_user.o mem.o mem_user.o mmu.o process.o \
	process_kern.o syscall_kern.o syscall_user.o time.o tlb.o trap_user.o \
	sys-$(SUBARCH)/

USER_OBJS = $(filter %_user.o,$(obj-y)) process.o time.o
USER_OBJS := $(foreach file,$(USER_OBJS),$(obj)/$(file))

include/skas_ptregs.h : util/mk_ptregs
	util/mk_ptregs > $@

util/mk_ptregs :
	$(MAKE) -C util

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$(notdir $@)) $(USER_CFLAGS) -c -o $@ $<

clean :
	$(MAKE) -C util clean
	$(RM) -f include/skas_ptregs.h
