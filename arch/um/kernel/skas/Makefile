# 
# Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

subdir-y = sys-$(SUBARCH)/

obj-y = exec_kern.o exec_user.o mem_user.o mmu.o process.o process_kern.o \
	syscall_user.o trap_user.o $(subdir-y)

USER_OBJS = $(filter %_user.o,$(obj-y)) process.o time.o
USER_OBJS := $(foreach file,$(USER_OBJS),$(obj)/$(file))

include $(TOPDIR)/Rules.make

include/skas_ptregs.h : util/mk_ptregs
	util/mk_ptregs > $@

util/mk_ptregs :
	$(MAKE) -C util

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$(notdir $@)) $(USER_CFLAGS) -c -o $@ $<

clean :
	$(MAKE) -C util clean
	$(RM) -f include/skas_ptregs.h
