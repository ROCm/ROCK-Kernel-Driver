/* 
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/slab.h"
#include "linux/smp_lock.h"
#include "linux/ptrace.h"
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
#include "time_user.h"
#include "choose-mode.h"
#include "mode_kern.h"

void flush_thread(void)
{
	CHOOSE_MODE(flush_thread_tt(), flush_thread_skas());
}

void start_thread(struct pt_regs *regs, unsigned long eip, unsigned long esp)
{
	CHOOSE_MODE_PROC(start_thread_tt, start_thread_skas, regs, eip, esp);
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
	int err;

	err = execve1(file, argv, env);
	if(!err) 
		do_longjmp(current->thread.exec_buf, 1);
	return(err);
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
