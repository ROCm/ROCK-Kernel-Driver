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

int copy_sc_from_user_skas(union uml_pt_regs *regs, void *from_ptr)
{
  	struct sigcontext sc, *from = from_ptr;
	unsigned long fpregs[FP_FRAME_SIZE];
	int err;

	err = copy_from_user_proc(&sc, from, sizeof(sc));
	err |= copy_from_user_proc(fpregs, sc.fpstate, sizeof(fpregs));
	if(err)
		return(err);

	regs->skas.regs[GS] = sc.gs;
	regs->skas.regs[FS] = sc.fs;
	regs->skas.regs[ES] = sc.es;
	regs->skas.regs[DS] = sc.ds;
	regs->skas.regs[EDI] = sc.edi;
	regs->skas.regs[ESI] = sc.esi;
	regs->skas.regs[EBP] = sc.ebp;
	regs->skas.regs[UESP] = sc.esp;
	regs->skas.regs[EBX] = sc.ebx;
	regs->skas.regs[EDX] = sc.edx;
	regs->skas.regs[ECX] = sc.ecx;
	regs->skas.regs[EAX] = sc.eax;
	regs->skas.regs[EIP] = sc.eip;
	regs->skas.regs[CS] = sc.cs;
	regs->skas.regs[EFL] = sc.eflags;
	regs->skas.regs[UESP] = sc.esp_at_signal;
	regs->skas.regs[SS] = sc.ss;
	regs->skas.fault_addr = sc.cr2;
	regs->skas.fault_type = FAULT_WRITE(sc.err);
	regs->skas.trap_type = sc.trapno;

	err = ptrace(PTRACE_SETFPREGS, userspace_pid, 0, fpregs);
	if(err < 0){
	  	printk("copy_sc_to_user - PTRACE_SETFPREGS failed, "
		       "errno = %d\n", errno);
		return(1);
	}

	return(0);
}

int copy_sc_to_user_skas(void *to_ptr, void *fp, union uml_pt_regs *regs, 
			 unsigned long fault_addr, int fault_type)
{
  	struct sigcontext sc, *to = to_ptr;
	struct _fpstate *to_fp;
	unsigned long fpregs[FP_FRAME_SIZE];
	int err;

	sc.gs = regs->skas.regs[GS];
	sc.fs = regs->skas.regs[FS];
	sc.es = regs->skas.regs[ES];
	sc.ds = regs->skas.regs[DS];
	sc.edi = regs->skas.regs[EDI];
	sc.esi = regs->skas.regs[ESI];
	sc.ebp = regs->skas.regs[EBP];
	sc.esp = regs->skas.regs[UESP];
	sc.ebx = regs->skas.regs[EBX];
	sc.edx = regs->skas.regs[EDX];
	sc.ecx = regs->skas.regs[ECX];
	sc.eax = regs->skas.regs[EAX];
	sc.eip = regs->skas.regs[EIP];
	sc.cs = regs->skas.regs[CS];
	sc.eflags = regs->skas.regs[EFL];
	sc.esp_at_signal = regs->skas.regs[UESP];
	sc.ss = regs->skas.regs[SS];
	sc.cr2 = fault_addr;
	sc.err = TO_SC_ERR(fault_type);
	sc.trapno = regs->skas.trap_type;

	err = ptrace(PTRACE_GETFPREGS, userspace_pid, 0, fpregs);
	if(err < 0){
	  	printk("copy_sc_to_user - PTRACE_GETFPREGS failed, "
		       "errno = %d\n", errno);
		return(1);
	}
	to_fp = (struct _fpstate *) 
		(fp ? (unsigned long) fp : ((unsigned long) to + sizeof(*to)));
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
