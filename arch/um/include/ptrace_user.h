/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __PTRACE_USER_H__
#define __PTRACE_USER_H__

#include "sysdep/ptrace_user.h"

extern int ptrace_getregs(long pid, unsigned long *regs_out);
extern int ptrace_setregs(long pid, unsigned long *regs_in);
extern int ptrace_getfpregs(long pid, unsigned long *regs_out);
extern void arch_enter_kernel(void *task, int pid);
extern void arch_leave_kernel(void *task, int pid);
extern void ptrace_pokeuser(unsigned long addr, unsigned long data);


/* syscall emulation path in ptrace */

#ifndef PTRACE_SYSEMU
#define PTRACE_SYSEMU 31
#endif

void set_using_sysemu(int value);
int get_using_sysemu(void);
extern int sysemu_supported;

#endif
