/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "asm/mman.h"
#include "asm/uaccess.h"
#include "asm/unistd.h"

/*
 * Perform the select(nd, in, out, ex, tv) and mmap() system
 * calls. Linux/i386 didn't use to be able to handle more than
 * 4 system call parameters, so these system calls used a memory
 * block for parameter passing..
 */

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

extern int old_mmap(unsigned long addr, unsigned long len,
		    unsigned long prot, unsigned long flags,
		    unsigned long fd, unsigned long offset);

int old_mmap_i386(struct mmap_arg_struct *arg)
{
	struct mmap_arg_struct a;
	int err = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;

	err = old_mmap(a.addr, a.len, a.prot, a.flags, a.fd, a.offset);
 out:
	return err;
}

struct sel_arg_struct {
	unsigned long n;
	fd_set *inp, *outp, *exp;
	struct timeval *tvp;
};

int old_select(struct sel_arg_struct *arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	/* sys_select() does the appropriate kernel locking */
	return sys_select(a.n, a.inp, a.outp, a.exp, a.tvp);
}

/* The i386 version skips reading from %esi, the fourth argument. So we must do
 * this, too.
 */
int sys_clone(unsigned long clone_flags, unsigned long newsp, int *parent_tid,
	      int unused, int *child_tid)
{
	long ret;

	/* XXX: normal arch do here this pass, and also pass the regs to
	 * do_fork, instead of NULL. Currently the arch-independent code
	 * ignores these values, while the UML code (actually it's
	 * copy_thread) does the right thing. But this should change,
	 probably. */
	/*if (!newsp)
		newsp = UPT_SP(current->thread.regs);*/
	current->thread.forking = 1;
	ret = do_fork(clone_flags, newsp, NULL, 0, parent_tid, child_tid);
	current->thread.forking = 0;
	return(ret);
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
