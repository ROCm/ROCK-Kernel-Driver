/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/types.h"
#include "linux/utime.h"
#include "linux/sys.h"
#include "linux/ptrace.h"
#include "asm/unistd.h"
#include "asm/ptrace.h"
#include "asm/uaccess.h"
#include "asm/stat.h"
#include "sysdep/syscalls.h"
#include "kern_util.h"

static inline int check_area(void *ptr, int size)
{
	return(verify_area(VERIFY_WRITE, ptr, size));
}

static int check_readlink(struct pt_regs *regs)
{
	return(check_area((void *) UPT_SYSCALL_ARG1(&regs->regs),
			  UPT_SYSCALL_ARG2(&regs->regs)));
}

static int check_utime(struct pt_regs *regs)
{
	return(check_area((void *) UPT_SYSCALL_ARG1(&regs->regs),
			  sizeof(struct utimbuf)));
}

static int check_oldstat(struct pt_regs *regs)
{
	return(check_area((void *) UPT_SYSCALL_ARG1(&regs->regs), 
			  sizeof(struct __old_kernel_stat)));
}

static int check_stat(struct pt_regs *regs)
{
	return(check_area((void *) UPT_SYSCALL_ARG1(&regs->regs), 
			  sizeof(struct stat)));
}

static int check_stat64(struct pt_regs *regs)
{
	return(check_area((void *) UPT_SYSCALL_ARG1(&regs->regs), 
			  sizeof(struct stat64)));
}

struct bogus {
	int kernel_ds;
	int (*check_params)(struct pt_regs *);
};

struct bogus this_is_bogus[256] = {
	[ __NR_mknod ] = { 1, NULL },
	[ __NR_mkdir ] = { 1, NULL },
	[ __NR_rmdir ] = { 1, NULL },
	[ __NR_unlink ] = { 1, NULL },
	[ __NR_symlink ] = { 1, NULL },
	[ __NR_link ] = { 1, NULL },
	[ __NR_rename ] = { 1, NULL },
	[ __NR_umount ] = { 1, NULL },
	[ __NR_mount ] = { 1, NULL },
	[ __NR_pivot_root ] = { 1, NULL },
	[ __NR_chdir ] = { 1, NULL },
	[ __NR_chroot ] = { 1, NULL },
	[ __NR_open ] = { 1, NULL },
	[ __NR_quotactl ] = { 1, NULL },
	[ __NR_sysfs ] = { 1, NULL },
	[ __NR_readlink ] = { 1, check_readlink },
	[ __NR_acct ] = { 1, NULL },
	[ __NR_execve ] = { 1, NULL },
	[ __NR_uselib ] = { 1, NULL },
	[ __NR_statfs ] = { 1, NULL },
	[ __NR_truncate ] = { 1, NULL },
	[ __NR_access ] = { 1, NULL },
	[ __NR_chmod ] = { 1, NULL },
	[ __NR_chown ] = { 1, NULL },
	[ __NR_lchown ] = { 1, NULL },
	[ __NR_utime ] = { 1, check_utime },
	[ __NR_oldlstat ] = { 1, check_oldstat },
	[ __NR_oldstat ] = { 1, check_oldstat },
	[ __NR_stat ] = { 1, check_stat },
	[ __NR_lstat ] = { 1, check_stat },
	[ __NR_stat64 ] = { 1, check_stat64 },
	[ __NR_lstat64 ] = { 1, check_stat64 },
	[ __NR_chown32 ] = { 1, NULL },
};

/* sys_utimes */

static int check_bogosity(struct pt_regs *regs)
{
	struct bogus *bogon = &this_is_bogus[UPT_SYSCALL_NR(&regs->regs)];

	if(!bogon->kernel_ds) return(0);
	if(bogon->check_params && (*bogon->check_params)(regs))
		return(-EFAULT);
	set_fs(KERNEL_DS);
	return(0);
}

extern syscall_handler_t *sys_call_table[];

long execute_syscall_tt(void *r)
{
	struct pt_regs *regs = r;
	long res;
	int syscall;

	current->thread.nsyscalls++;
	nsyscalls++;
	syscall = UPT_SYSCALL_NR(&regs->regs);

	if((syscall >= NR_syscalls) || (syscall < 0))
		res = -ENOSYS;
	else if(honeypot && check_bogosity(regs))
		res = -EFAULT;
	else res = EXECUTE_SYSCALL(syscall, regs);

	set_fs(USER_DS);

	if(current->thread.mode.tt.singlestep_syscall){
		current->thread.mode.tt.singlestep_syscall = 0;
		current->ptrace &= ~PT_DTRACE;
		force_sig(SIGTRAP, current);
	}

	return(res);
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
