# 
# Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

obj-y = exec_kern.o exec_user.o mem.o mem_user.o mmu.o process.o \
	process_kern.o syscall_kern.o syscall_user.o time.o tlb.o trap_user.o \
	uaccess.o sys-$(SUBARCH)/

host-progs	:= util/mk_ptregs
clean-files	:= include/skas_ptregs.h

USER_OBJS = $(filter %_user.o,$(obj-y)) process.o time.o
USER_OBJS := $(foreach file,$(USER_OBJS),$(obj)/$(file))

$(TOPDIR)/arch/um/include/skas_ptregs.h : $(src)/util/mk_ptregs
	@echo -n '  Generating $@'
	@$< > $@.tmp
	@if [ -r $@ ] && cmp -s $@ $@.tmp; then \
		echo ' (unchanged)'; \
		rm -f $@.tmp; \
	else \
		echo ' (updated)'; \
		mv -f $@.tmp $@; \
	fi

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$(notdir $@)) $(USER_CFLAGS) -c -o $@ $<
