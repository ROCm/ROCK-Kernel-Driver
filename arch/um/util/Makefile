EXTRA_TARGETS := mk_task mk_task_kern.o

include $(TOPDIR)/Rules.make

$(obj)/mk_task: $(obj)/mk_task_user.o $(obj)/mk_task_kern.o
	$(CC) -o $@ $^

$(obj)/mk_task_user.o: $(src)/mk_task_user.c
	$(CC) -o $@ -c $< 

clean:
	$(RM) $(EXTRA_TARGETS)

archmrproper:
