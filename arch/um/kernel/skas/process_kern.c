/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/slab.h"
#include "linux/ptrace.h"
#include "kern_util.h"
#include "time_user.h"
#include "signal_user.h"
#include "skas.h"
#include "os.h"
#include "user_util.h"
#include "tlb.h"
#include "frame.h"
#include "kern.h"
#include "mode.h"

int singlestepping_skas(void)
{
	int ret = current->ptrace & PT_DTRACE;

	current->ptrace &= ~PT_DTRACE;
	return(ret);
}

void *switch_to_skas(void *prev, void *next)
{
	struct task_struct *from, *to;

	from = prev;
	to = next;

	/* XXX need to check runqueues[cpu].idle */
	if(current->pid == 0)
		switch_timers(0);

	to->thread.prev_sched = from;
	set_current(to);

	switch_threads(&from->thread.mode.skas.switch_buf, 
		       to->thread.mode.skas.switch_buf);

	if(current->pid == 0)
		switch_timers(1);

	return(current->thread.prev_sched);
}

extern void schedule_tail(struct task_struct *prev);

void new_thread_handler(int sig)
{
	int (*fn)(void *), n;
	void *arg;

	fn = current->thread.request.u.thread.proc;
	arg = current->thread.request.u.thread.arg;
	change_sig(SIGUSR1, 1);
	thread_wait(&current->thread.mode.skas.switch_buf, 
		    current->thread.mode.skas.fork_buf);

#ifdef CONFIG_SMP
	schedule_tail(NULL);
#endif
	current->thread.prev_sched = NULL;

	n = run_kernel_thread(fn, arg, &current->thread.exec_buf);
	if(n == 1)
		userspace(&current->thread.regs.regs);
	else do_exit(0);
}

void new_thread_proc(void *stack, void (*handler)(int sig))
{
	init_new_thread_stack(stack, handler);
	os_usr1_process(os_getpid());
}

void release_thread_skas(struct task_struct *task)
{
}

void exit_thread_skas(void)
{
}

void fork_handler(int sig)
{
        change_sig(SIGUSR1, 1);
 	thread_wait(&current->thread.mode.skas.switch_buf, 
		    current->thread.mode.skas.fork_buf);
  	
	force_flush_all();
#ifdef CONFIG_SMP
	schedule_tail(current->thread.prev_sched);
#endif
	current->thread.prev_sched = NULL;
	unblock_signals();

	userspace(&current->thread.regs.regs);
}

int copy_thread_skas(int nr, unsigned long clone_flags, unsigned long sp,
		     unsigned long stack_top, struct task_struct * p, 
		     struct pt_regs *regs)
{
  	void (*handler)(int);

	if(current->thread.forking){
	  	memcpy(&p->thread.regs.regs.skas, 
		       &current->thread.regs.regs.skas, 
		       sizeof(p->thread.regs.regs.skas));
		REGS_SET_SYSCALL_RETURN(p->thread.regs.regs.skas.regs, 0);
		if(sp != 0) REGS_SP(p->thread.regs.regs.skas.regs) = sp;

		handler = fork_handler;
	}
	else {
	  	memcpy(p->thread.regs.regs.skas.regs, exec_regs, 
		       sizeof(p->thread.regs.regs.skas.regs));
		memcpy(p->thread.regs.regs.skas.fp, exec_fp_regs, 
		       sizeof(p->thread.regs.regs.skas.fp));
	  	memcpy(p->thread.regs.regs.skas.xfp, exec_fpx_regs, 
		       sizeof(p->thread.regs.regs.skas.xfp));
                p->thread.request.u.thread = current->thread.request.u.thread;
		handler = new_thread_handler;
	}

	new_thread((void *) p->thread.kernel_stack, 
		   &p->thread.mode.skas.switch_buf, 
		   &p->thread.mode.skas.fork_buf, handler);
	return(0);
}

void init_idle_skas(void)
{
	cpu_tasks[current->thread_info->cpu].pid = os_getpid();
	default_idle();
}

extern void start_kernel(void);

static int start_kernel_proc(void *unused)
{
	int pid;

	block_signals();
	pid = os_getpid();

	cpu_tasks[0].pid = pid;
	cpu_tasks[0].task = current;
#ifdef CONFIG_SMP
 	cpu_online_map = cpumask_of_cpu(0);
#endif
	start_kernel();
	return(0);
}

int start_uml_skas(void)
{
	start_userspace();
	capture_signal_stack();

	init_new_thread_signals(1);
	idle_timer();

	init_task.thread.request.u.thread.proc = start_kernel_proc;
	init_task.thread.request.u.thread.arg = NULL;
	return(start_idle_thread((void *) init_task.thread.kernel_stack,
				 &init_task.thread.mode.skas.switch_buf,
				 &init_task.thread.mode.skas.fork_buf));
}

int external_pid_skas(struct task_struct *task)
{
	return(userspace_pid);
}

int thread_pid_skas(struct task_struct *task)
{
	return(userspace_pid);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
