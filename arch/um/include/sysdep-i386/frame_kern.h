/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __FRAME_KERN_I386_H
#define __FRAME_KERN_I386_H

/* This is called from sys_sigreturn.  It takes the sp at the point of the
 * sigreturn system call and returns the address of the sigcontext struct
 * on the stack.
 */

static inline void *sp_to_sc(unsigned long sp)
{
	return((void *) sp);
}

static inline void *sp_to_uc(unsigned long sp)
{
	unsigned long uc;

	uc = sp + signal_frame_si.uc_index - 
		signal_frame_si.common.sp_index - 4;
	return((void *) uc);
}

static inline void *sp_to_rt_sc(unsigned long sp)
{
	unsigned long sc;

	sc = sp - signal_frame_si.common.sp_index + 
		signal_frame_si.common.len - 4;
	return((void *) sc);
}

static inline void *sp_to_mask(unsigned long sp)
{
	unsigned long mask;

	mask = sp - signal_frame_sc.common.sp_index + 
		signal_frame_sc.common.len - 8;
	return((void *) mask);
}

extern int sc_size(void *data);

static inline void *sp_to_rt_mask(unsigned long sp)
{
	unsigned long mask;

	mask = sp - signal_frame_si.common.sp_index + 
		signal_frame_si.common.len + 
		sc_size(&signal_frame_si.common.arch) - 4;
	return((void *) mask);
}

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
