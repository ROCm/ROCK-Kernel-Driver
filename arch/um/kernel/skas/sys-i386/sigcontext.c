/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <errno.h>
#include <asm/sigcontext.h>
#include <sys/ptrace.h>
#include <linux/ptrace.h>
#include "sysdep/ptrace.h"
#include "sysdep/ptrace_user.h"
#include "kern_util.h"
#include "user.h"
#include "sigcontext.h"

extern int userspace_pid;

int copy_sc_from_user_skas(struct uml_pt_regs *regs, void *from_ptr)
{
  	struct sigcontext sc, *from = from_ptr;
	unsigned long fpregs[FP_FRAME_SIZE];
	int err;

	err = copy_from_user_proc(&sc, from, sizeof(sc));
	err |= copy_from_user_proc(fpregs, sc.fpstate, sizeof(fpregs));
	if(err)
		return(err);

	regs->mode.skas.regs[GS] = sc.gs;
	regs->mode.skas.regs[FS] = sc.fs;
	regs->mode.skas.regs[ES] = sc.es;
	regs->mode.skas.regs[DS] = sc.ds;
	regs->mode.skas.regs[EDI] = sc.edi;
	regs->mode.skas.regs[ESI] = sc.esi;
	regs->mode.skas.regs[EBP] = sc.ebp;
	regs->mode.skas.regs[UESP] = sc.esp;
	regs->mode.skas.regs[EBX] = sc.ebx;
	regs->mode.skas.regs[EDX] = sc.edx;
	regs->mode.skas.regs[ECX] = sc.ecx;
	regs->mode.skas.regs[EAX] = sc.eax;
	regs->mode.skas.regs[EIP] = sc.eip;
	regs->mode.skas.regs[CS] = sc.cs;
	regs->mode.skas.regs[EFL] = sc.eflags;
	regs->mode.skas.regs[UESP] = sc.esp_at_signal;
	regs->mode.skas.regs[SS] = sc.ss;
	regs->mode.skas.fault_addr = sc.cr2;
	regs->mode.skas.fault_type = FAULT_WRITE(sc.err);
	regs->mode.skas.trap_type = sc.trapno;

	err = ptrace(PTRACE_SETFPREGS, userspace_pid, 0, fpregs);
	if(err < 0){
	  	printk("copy_sc_to_user - PTRACE_SETFPREGS failed, "
		       "errno = %d\n", errno);
		return(1);
	}

	return(0);
}

int copy_sc_to_user_skas(void *to_ptr, struct uml_pt_regs *regs, 
			 unsigned long fault_addr, int fault_type)
{
  	struct sigcontext sc, *to = to_ptr;
	struct _fpstate *to_fp;
	unsigned long fpregs[FP_FRAME_SIZE];
	int err;

	sc.gs = regs->mode.skas.regs[GS];
	sc.fs = regs->mode.skas.regs[FS];
	sc.es = regs->mode.skas.regs[ES];
	sc.ds = regs->mode.skas.regs[DS];
	sc.edi = regs->mode.skas.regs[EDI];
	sc.esi = regs->mode.skas.regs[ESI];
	sc.ebp = regs->mode.skas.regs[EBP];
	sc.esp = regs->mode.skas.regs[UESP];
	sc.ebx = regs->mode.skas.regs[EBX];
	sc.edx = regs->mode.skas.regs[EDX];
	sc.ecx = regs->mode.skas.regs[ECX];
	sc.eax = regs->mode.skas.regs[EAX];
	sc.eip = regs->mode.skas.regs[EIP];
	sc.cs = regs->mode.skas.regs[CS];
	sc.eflags = regs->mode.skas.regs[EFL];
	sc.esp_at_signal = regs->mode.skas.regs[UESP];
	sc.ss = regs->mode.skas.regs[SS];
	sc.cr2 = fault_addr;
	sc.err = TO_SC_ERR(fault_type);
	sc.trapno = regs->mode.skas.trap_type;

	err = ptrace(PTRACE_GETFPREGS, userspace_pid, 0, fpregs);
	if(err < 0){
	  	printk("copy_sc_to_user - PTRACE_GETFPREGS failed, "
		       "errno = %d\n", errno);
		return(1);
	}
	to_fp = (struct _fpstate *)((unsigned long) to + sizeof(*to));
	sc.fpstate = to_fp;

	if(err)
	  	return(err);

	return(copy_to_user_proc(to, &sc, sizeof(sc)) ||
	       copy_to_user_proc(to_fp, fpregs, sizeof(fpregs)));
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
