include $(TOPDIR)/Rules.make

all : mk_task

mk_task : mk_task_user.o mk_task_kern.o
	$(CC) -o mk_task mk_task_user.o mk_task_kern.o

mk_task_user.o : mk_task_user.c
	$(CC) -c $< 

clean :
	$(RM) mk_task *.o *~

archmrproper : clean
