/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "asm/ptrace.h"
#include "asm/uaccess.h"
#include "asm/signal.h"
#include "asm/uaccess.h"
#include "asm/ucontext.h"
#include "frame_kern.h"
#include "sigcontext.h"
#include "sysdep/ptrace.h"
#include "choose-mode.h"
#include "mode.h"

static int copy_restorer(void (*restorer)(void), unsigned long start, 
			 unsigned long sr_index, int sr_relative)
{
	unsigned long sr;

	if(sr_relative){
		sr = (unsigned long) restorer;
		sr += start + sr_index;
		restorer = (void (*)(void)) sr;
	}

	return(copy_to_user((void *) (start + sr_index), &restorer, 
			    sizeof(restorer)));
}

static int copy_sc_to_user(void *to, void *fp, struct pt_regs *from, 
			   struct arch_frame_data *arch)
{
	return(CHOOSE_MODE(copy_sc_to_user_tt(to, fp, UPT_SC(&from->regs), 
					      arch),
			   copy_sc_to_user_skas(to, fp, &from->regs,
						current->thread.cr2,
						current->thread.err)));
}

static int copy_ucontext_to_user(struct ucontext *uc, void *fp, sigset_t *set,
				 unsigned long sp)
{
	int err = 0;

	err |= put_user(current->sas_ss_sp, &uc->uc_stack.ss_sp);
	err |= put_user(sas_ss_flags(sp), &uc->uc_stack.ss_flags);
	err |= put_user(current->sas_ss_size, &uc->uc_stack.ss_size);
	err |= copy_sc_to_user(&uc->uc_mcontext, fp, &current->thread.regs,
			       &signal_frame_si.common.arch);
	err |= copy_to_user(&uc->uc_sigmask, set, sizeof(*set));
	return(err);
}

int setup_signal_stack_si(unsigned long stack_top, int sig, 
			  unsigned long handler, void (*restorer)(void), 
			  struct pt_regs *regs, siginfo_t *info, 
			  sigset_t *mask)
{
	unsigned long start;
	void *sip, *ucp, *fp;

	start = stack_top - signal_frame_si.common.len;
	sip = (void *) (start + signal_frame_si.si_index);
	ucp = (void *) (start + signal_frame_si.uc_index);
	fp = (void *) (((unsigned long) ucp) + sizeof(struct ucontext));

	if(restorer == NULL)
		panic("setup_signal_stack_si - no restorer");

	if(copy_to_user((void *) start, signal_frame_si.common.data,
			signal_frame_si.common.len) ||
	   copy_to_user((void *) (start + signal_frame_si.common.sig_index), 
			&sig, sizeof(sig)) ||
	   copy_siginfo_to_user(sip, info) ||
	   copy_to_user((void *) (start + signal_frame_si.sip_index), &sip,
			sizeof(sip)) ||
	   copy_ucontext_to_user(ucp, fp, mask, PT_REGS_SP(regs)) ||
	   copy_to_user((void *) (start + signal_frame_si.ucp_index), &ucp,
			sizeof(ucp)) ||
	   copy_restorer(restorer, start, signal_frame_si.common.sr_index,
			 signal_frame_si.common.sr_relative))
		return(1);
	
	PT_REGS_IP(regs) = handler;
	PT_REGS_SP(regs) = start + signal_frame_si.common.sp_index;
	return(0);
}

int setup_signal_stack_sc(unsigned long stack_top, int sig, 
			  unsigned long handler, void (*restorer)(void), 
			  struct pt_regs *regs, sigset_t *mask)
{
	struct frame_common *frame = &signal_frame_sc_sr.common;
	void *user_sc;
	int sig_size = (_NSIG_WORDS - 1) * sizeof(unsigned long);
	unsigned long sigs, sr;
	unsigned long start = stack_top - frame->len - sig_size;

	user_sc = (void *) (start + signal_frame_sc_sr.sc_index);
	if(restorer == NULL){
		frame = &signal_frame_sc.common;
		user_sc = (void *) (start + signal_frame_sc.sc_index);
		sr = (unsigned long) frame->data;
		sr += frame->sr_index;
		sr = *((unsigned long *) sr);
		restorer = ((void (*)(void)) sr);
	}

	sigs = start + frame->len;
	if(copy_to_user((void *) start, frame->data, frame->len) ||
	   copy_to_user((void *) (start + frame->sig_index), &sig, 
			sizeof(sig)) ||
	   copy_sc_to_user(user_sc, NULL, regs, 
			   &signal_frame_sc.common.arch) ||
	   copy_to_user(sc_sigmask(user_sc), mask, sizeof(mask->sig[0])) ||
	   copy_to_user((void *) sigs, &mask->sig[1], sig_size) ||
	   copy_restorer(restorer, start, frame->sr_index, frame->sr_relative))
		return(1);

	PT_REGS_IP(regs) = handler;
	PT_REGS_SP(regs) = start + frame->sp_index;

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
