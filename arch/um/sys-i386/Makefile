
obj-y = bugs.o checksum.o extable.o fault.o ksyms.o ldt.o old-checksum.o \
	ptrace.o ptrace_user.o semaphore.o sigcontext.o syscalls.o sysrq.o

export-objs = ksyms.o

USER_OBJS = bugs.o ptrace_user.o sigcontext.o fault.o

SYMLINKS = semaphore.c old-checksum.c checksum.S extable.c

include $(TOPDIR)/Rules.make

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$@) $(USER_CFLAGS) -c -o $@ $<

checksum.S old-checksum.c: 
	-rm -f $@
	-ln -s $(TOPDIR)/arch/i386/lib/$@ $@

semaphore.c:
	-rm -f $@
	-ln -s $(TOPDIR)/arch/i386/kernel/$@ $@

extable.c:
	-rm -f $@
	-ln -s $(TOPDIR)/arch/i386/mm/$@ $@

clean:
	$(MAKE) -C util clean

fastdep:

dep:

archmrproper:
	rm -f $(SYMLINKS)

archclean:

archdep:

modules:

