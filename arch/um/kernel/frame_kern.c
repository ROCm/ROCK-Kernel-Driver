/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "asm/ptrace.h"
#include "asm/uaccess.h"
#include "asm/signal.h"
#include "frame_kern.h"
#include "sigcontext.h"
#include "sysdep/ptrace.h"

static int copy_restorer(void (*restorer)(void), unsigned long start, 
			 unsigned long sr_index, int sr_relative)
{
	if(restorer != 0){
		if(copy_to_user((void *) (start + sr_index), &restorer, 
				sizeof(restorer)))
			return(1);
	}
	else if(sr_relative){
		unsigned long *sr = (unsigned long *) (start + sr_index);
		*sr += (unsigned long) sr;
	}
	return(0);
}

int setup_signal_stack_si(unsigned long stack_top, int sig, 
			  unsigned long handler, void (*restorer)(void), 
			  struct pt_regs *regs, siginfo_t *info, 
			  sigset_t *mask)
{
	unsigned long start, sc, sigs;
	void *sip;
	int sig_size = _NSIG_WORDS * sizeof(unsigned long);

	start = stack_top - signal_frame_si.len - 
		sc_size(&signal_frame_sc.arch) - sig_size;
	sip = (void *) (start + signal_frame_si.si_index);
	sc = start + signal_frame_si.len;
	sigs = sc + sc_size(&signal_frame_sc.arch);
	if(copy_sc_to_user((void *) sc, regs->regs.sc, 
			   &signal_frame_sc.arch) ||
	   copy_to_user((void *) start, signal_frame_si.data,
			signal_frame_si.len) ||
	   copy_to_user((void *) (start + signal_frame_si.sig_index), &sig, 
			sizeof(sig)) ||
	   copy_siginfo_to_user(sip, info) ||
	   copy_to_user((void *) (start + signal_frame_si.sip_index), &sip,
			sizeof(sip)) ||
	   copy_to_user((void *) sigs, mask, sig_size) ||
	   copy_restorer(restorer, start, signal_frame_si.sr_index,
			 signal_frame_si.sr_relative))
		return(1);
	
	PT_REGS_IP(regs) = handler;
	PT_REGS_SP(regs) = start + signal_frame_sc.sp_index;
	return(0);
}

int setup_signal_stack_sc(unsigned long stack_top, int sig, 
			  unsigned long handler, void (*restorer)(void), 
			  struct pt_regs *regs, sigset_t *mask)
{
	int sig_size = (_NSIG_WORDS - 1) * sizeof(unsigned long);
	unsigned long sigs, start = stack_top - signal_frame_sc.len - sig_size;
	void *user_sc = (void *) (start + signal_frame_sc.sc_index);

	sigs = start + signal_frame_sc.len;
	if(copy_to_user((void *) start, signal_frame_sc.data, 
			signal_frame_sc.len) ||
	   copy_to_user((void *) (start + signal_frame_sc.sig_index), &sig,
			sizeof(sig)) ||
	   copy_sc_to_user(user_sc, regs->regs.sc, &signal_frame_sc.arch) ||
	   copy_to_user(sc_sigmask(user_sc), mask, sizeof(mask->sig[0])) ||
	   copy_to_user((void *) sigs, &mask->sig[1], sig_size) ||
	   copy_restorer(restorer, start, signal_frame_sc.sr_index,
			 signal_frame_sc.sr_relative))
		return(1);

	PT_REGS_IP(regs) = handler;
	PT_REGS_SP(regs) = start + signal_frame_sc.sp_index;

	set_sc_ip_sp(regs->regs.sc, handler, start + signal_frame_sc.sp_index);
	return(0);
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
