host-progs	:= mk_sc mk_thread
always		:= $(host-progs)

mk_thread-objs	:= mk_thread_kern.o mk_thread_user.o

HOSTCFLAGS_mk_thread_kern.o	:= $(CPPFLAGS) $(CFLAGS)
HOSTCFLAGS_mk_thread_user.o	:= $(USER_CFLAGS)

clean :
	$(RM) -f $(build-targets)

archmrproper : clean
