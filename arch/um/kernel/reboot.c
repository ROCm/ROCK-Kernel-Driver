/* 
 * Copyright (C) 2000, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "os.h"

#ifdef CONFIG_SMP
static void kill_idlers(int me)
{
	struct task_struct *p;
	int i;

	for(i = 0; i < sizeof(idle_threads)/sizeof(idle_threads[0]); i++){
		p = idle_threads[i];
		if((p != NULL) && (p->thread.extern_pid != me))
			os_kill_process(p->thread.extern_pid, 0);
	}
}
#endif

static void kill_off_processes(void)
{
	struct task_struct *p;
	int me;

	me = os_getpid();
	for_each_process(p){
		if(p->thread.extern_pid != me) 
			os_kill_process(p->thread.extern_pid, 0);
	}
	if(init_task.thread.extern_pid != me) 
		os_kill_process(init_task.thread.extern_pid, 0);
#ifdef CONFIG_SMP
	kill_idlers(me);
#endif
}

void uml_cleanup(void)
{
	kill_off_processes();
	do_uml_exitcalls();
}

void machine_restart(char * __unused)
{
	do_uml_exitcalls();
	kill_off_processes();
	tracing_reboot();
	os_kill_process(os_getpid(), 0);
}

void machine_power_off(void)
{
	do_uml_exitcalls();
	kill_off_processes();
	tracing_halt();
	os_kill_process(os_getpid(), 0);
}

void machine_halt(void)
{
	machine_power_off();
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
