host-progs = mk_sc

mk_sc-objs = mk_sc.o

include $(TOPDIR)/Rules.make

mk_thread : mk_thread_kern.o mk_thread_user.o
	$(CC) $(CFLAGS) -o $@ mk_thread_user.o mk_thread_kern.o

mk_thread_user.o : mk_thread_user.c
	$(CC) $(USER_CFLAGS) -c -o $@ $<

clean :
	$(RM) $(host-progs) mk_thread

archmrproper : clean
