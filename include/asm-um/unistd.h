/* 
 * Copyright (C) 2000, 2001  Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef _UM_UNISTD_H_
#define _UM_UNISTD_H_

#include "linux/resource.h"
#include "asm/uaccess.h"

extern long sys_open(const char *filename, int flags, int mode);
extern long sys_dup(unsigned int fildes);
extern long sys_close(unsigned int fd);
extern int um_execve(const char *file, char *const argv[], char *const env[]);
extern long sys_setsid(void);
extern long sys_waitpid(pid_t pid, unsigned int * stat_addr, int options);
extern long sys_wait4(pid_t pid,unsigned int *stat_addr, int options, 
		      struct rusage *ru);
extern long sys_mount(char *dev_name, char *dir_name, char *type, 
		      unsigned long flags, void *data);
extern long sys_select(int n, fd_set *inp, fd_set *outp, fd_set *exp, 
		       struct timeval *tvp);
extern long sys_lseek(unsigned int fildes, unsigned long offset, int whence);
extern long sys_read(unsigned int fildes, char *buf, int len);
extern long sys_write(int fildes, const char *buf, size_t len);

#ifdef __KERNEL_SYSCALLS__

#define KERNEL_CALL(ret_t, sys, args...)	\
	mm_segment_t fs = get_fs();		\
	ret_t ret;				\
	set_fs(KERNEL_DS);			\
	ret = sys(args);			\
	set_fs(fs);				\
	return ret;

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

static inline long lseek(unsigned int fd, off_t offset, unsigned int whence)
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
