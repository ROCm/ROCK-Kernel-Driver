always		:= mk_task mk_constants
targets		:= mk_task_user.o mk_task_kern.o \
		   mk_constants_user.o mk_constants_kern.o

$(obj)/mk_task: $(obj)/mk_task_user.o $(obj)/mk_task_kern.o
	$(HOSTCC) -o $@ $^

$(obj)/mk_task_user.o: $(src)/mk_task_user.c
	$(HOSTCC) -o $@ -c $< 

$(obj)/mk_constants : $(obj)/mk_constants_user.o $(obj)/mk_constants_kern.o
	$(HOSTCC) -o $@ $^

$(obj)/mk_constants_user.o : $(src)/mk_constants_user.c
	$(HOSTCC) -c $< -o $@

$(obj)/mk_constants_kern.o : $(src)/mk_constants_kern.c
	$(HOSTCC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(build-targets)

archmrproper:
