EXTRA_TARGETS := mk_task mk_constants

$(obj)/mk_task: $(obj)/mk_task_user.o $(obj)/mk_task_kern.o
	$(CC) -o $@ $^

$(obj)/mk_task_user.o: $(src)/mk_task_user.c
	$(CC) -o $@ -c $< 

$(obj)/mk_constants : $(obj)/mk_constants_user.o $(obj)/mk_constants_kern.o
	$(CC) -o $@ $^

$(obj)/mk_constants_user.o : $(src)/mk_constants_user.c
	$(CC) -c $< -o $@

$(obj)/mk_constants_kern.o : $(src)/mk_constants_kern.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(EXTRA_TARGETS)

archmrproper:
