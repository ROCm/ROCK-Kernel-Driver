/* 
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SYSCALL_USER_H__
#define __SYSCALL_USER_H__

#include <asm/sigcontext.h>

extern void syscall_handler(int sig, struct uml_pt_regs *regs);
extern void exit_kernel(int pid, void *task);
extern int do_syscall(void *task, int pid);

#endif

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
