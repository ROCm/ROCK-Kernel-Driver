#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

/* Second argument to futex syscall */
#define FUTEX_WAIT (0)
#define FUTEX_WAKE (1)
#define FUTEX_FD (2)

extern asmlinkage long sys_futex(u32 __user *uaddr, int op, int val, struct timespec __user *utime);

#endif
