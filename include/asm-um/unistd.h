/* 
 * Copyright (C) 2000, 2001  Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef _UM_UNISTD_H_
#define _UM_UNISTD_H_

#include <linux/syscalls.h>
#include "linux/resource.h"
#include "asm/uaccess.h"

extern int um_execve(const char *file, char *const argv[], char *const env[]);

#ifdef __KERNEL__
/* We get __ARCH_WANT_OLD_STAT and __ARCH_WANT_STAT64 from the base arch */
#define __ARCH_WANT_IPC_PARSE_VERSION
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SGETMASK
#define __ARCH_WANT_SYS_SIGNAL
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_WAITPID
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_OLD_GETRLIMIT
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#define __ARCH_WANT_SYS_RT_SIGACTION
#endif

#ifdef __KERNEL_SYSCALLS__

#include <linux/compiler.h>
#include <linux/types.h>

#define KERNEL_CALL(ret_t, sys, args...)	\
	mm_segment_t fs = get_fs();		\
	ret_t ret;				\
	set_fs(KERNEL_DS);			\
	ret = sys(args);			\
	set_fs(fs);				\
	if (ret >= 0)				\
		return ret;			\
	errno = -(long)ret;			\
	return -1;

static inline long open(const char *pathname, int flags, int mode) 
{
	KERNEL_CALL(int, sys_open, pathname, flags, mode)
}

static inline long dup(unsigned int fd)
{
	KERNEL_CALL(int, sys_dup, fd);
}

static inline long close(unsigned int fd)
{
	KERNEL_CALL(int, sys_close, fd);
}

static inline int execve(const char *filename, char *const argv[], 
			 char *const envp[])
{
	KERNEL_CALL(int, um_execve, filename, argv, envp);
}

static inline long waitpid(pid_t pid, unsigned int *status, int options)
{
	KERNEL_CALL(pid_t, sys_wait4, pid, status, options, NULL)
}

static inline pid_t setsid(void)
{
	KERNEL_CALL(pid_t, sys_setsid)
}

static inline off_t lseek(unsigned int fd, off_t offset, unsigned int whence)
{
	KERNEL_CALL(long, sys_lseek, fd, offset, whence)
}

static inline int read(unsigned int fd, char * buf, int len)
{
	KERNEL_CALL(int, sys_read, fd, buf, len)
}

static inline int write(unsigned int fd, char * buf, int len)
{
	KERNEL_CALL(int, sys_write, fd, buf, len)
}

long sys_mmap2(unsigned long addr, unsigned long len,
		unsigned long prot, unsigned long flags,
		unsigned long fd, unsigned long pgoff);
long sys_execve(char *file, char **argv, char **env);
long sys_clone(unsigned long clone_flags, unsigned long newsp,
		int *parent_tid, int *child_tid);
long sys_fork(void);
long sys_vfork(void);
long sys_pipe(unsigned long *fildes);
struct sigaction;
asmlinkage long sys_rt_sigaction(int sig,
				const struct sigaction __user *act,
				struct sigaction __user *oact,
				size_t sigsetsize);

#endif

/* Save the value of __KERNEL_SYSCALLS__, undefine it, include the underlying
 * arch's unistd.h for the system call numbers, and restore the old 
 * __KERNEL_SYSCALLS__.
 */

#ifdef __KERNEL_SYSCALLS__
#define __SAVE_KERNEL_SYSCALLS__ __KERNEL_SYSCALLS__
#endif

#undef __KERNEL_SYSCALLS__
#include "asm/arch/unistd.h"

#ifdef __KERNEL_SYSCALLS__
#define __KERNEL_SYSCALLS__ __SAVE_KERNEL_SYSCALLS__
#endif

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
