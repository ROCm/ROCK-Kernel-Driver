EXTRA_TARGETS	:= mk_sc mk_thread mk_thread_kern.o

host-progs	:= mk_sc

mk_sc-objs	:= mk_sc.o

include $(TOPDIR)/Rules.make

$(obj)/mk_thread : $(obj)/mk_thread_kern.o $(obj)/mk_thread_user.o
	$(CC) $(CFLAGS) -o $@ $^

$(obj)/mk_thread_user.o : $(src)/mk_thread_user.c
	$(CC) $(USER_CFLAGS) -c -o $@ $<

clean :
	$(RM) -f $(EXTRA_TARGETS)

archmrproper : clean
