/* 
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/slab.h"
#include "linux/smp_lock.h"
#include "asm/ptrace.h"
#include "asm/pgtable.h"
#include "asm/tlbflush.h"
#include "asm/uaccess.h"
#include "user_util.h"
#include "kern_util.h"
#include "mem_user.h"
#include "kern.h"
#include "irq_user.h"
#include "tlb.h"
#include "2_5compat.h"
#include "os.h"

/* See comment above fork_tramp for why sigstop is defined and used like
 * this
 */

static int sigstop = SIGSTOP;

static int exec_tramp(void *sig_stack)
{
	int sig = sigstop;

	block_signals();
	init_new_thread(sig_stack, NULL);
	kill(os_getpid(), sig);
	return(0);
}

void flush_thread(void)
{
	unsigned long stack;
	int new_pid;

	stack = alloc_stack(0, 0);
	if(stack == 0){
		printk(KERN_ERR 
		       "flush_thread : failed to allocate temporary stack\n");
		do_exit(SIGKILL);
	}
		
	new_pid = start_fork_tramp((void *) current->thread.kernel_stack,
				   stack, 0, exec_tramp);
	if(new_pid < 0){
		printk(KERN_ERR 
		       "flush_thread : new thread failed, errno = %d\n",
		       -new_pid);
		do_exit(SIGKILL);
	}

	if(current->thread_info->cpu == 0)
		forward_interrupts(new_pid);
	current->thread.request.op = OP_EXEC;
	current->thread.request.u.exec.pid = new_pid;
	unprotect_stack((unsigned long) current->thread_info);
	os_usr1_process(os_getpid());

	free_page(stack);
	protect(uml_reserved, high_physmem - uml_reserved, 1, 1, 0, 1);
	task_protections((unsigned long) current->thread_info);
	force_flush_all();
	unblock_signals();
}

void start_thread(struct pt_regs *regs, unsigned long eip, unsigned long esp)
{
	set_fs(USER_DS);
	flush_tlb_mm(current->mm);
	PT_REGS_IP(regs) = eip;
	PT_REGS_SP(regs) = esp;
	PT_FIX_EXEC_STACK(esp);
}

static int execve1(char *file, char **argv, char **env)
{
        int error;

        error = do_execve(file, argv, env, &current->thread.regs);
        if (error == 0){
                current->ptrace &= ~PT_DTRACE;
                set_cmdline(current_cmd());
        }
        return(error);
}

int um_execve(char *file, char **argv, char **env)
{
	if(execve1(file, argv, env) == 0) do_longjmp(current->thread.jmp);
	return(-1);
}

int sys_execve(char *file, char **argv, char **env)
{
	int error;
	char *filename;

	lock_kernel();
	filename = getname((char *) file);
	error = PTR_ERR(filename);
	if (IS_ERR(filename)) goto out;
	error = execve1(filename, argv, env);
	putname(filename);
 out:
	unlock_kernel();
	return(error);
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
