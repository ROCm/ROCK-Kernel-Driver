
host-progs	:= mk_sc
always		:= $(host-progs) mk_thread
targets		:= mk_thread_kern.o mk_thread_user.o

mk_sc-objs	:= mk_sc.o

$(obj)/mk_thread : $(obj)/mk_thread_kern.o $(obj)/mk_thread_user.o
	$(CC) $(CFLAGS) -o $@ $^

$(obj)/mk_thread_user.o : $(src)/mk_thread_user.c
	$(CC) $(USER_CFLAGS) -c -o $@ $<

clean :
	$(RM) -f $(build-targets)

archmrproper : clean
