/*
 *  linux/kernel/compat.c
 *
 *  Kernel compatibililty routines for e.g. 32 bit syscall support
 *  on 64 bit kernels.
 *
 *  Copyright (C) 2002 Stephen Rothwell, IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/time.h>

#include <asm/uaccess.h>

static long compat_nanosleep_restart(struct restart_block *restart)
{
	unsigned long expire = restart->arg0, now = jiffies;
	struct timespec *rmtp = (struct timespec *) restart->arg1;
	long ret;

	/* Did it expire while we handled signals? */
	if (!time_after(expire, now))
		return 0;

	current->state = TASK_INTERRUPTIBLE;
	expire = schedule_timeout(expire - now);

	ret = 0;
	if (expire) {
		struct compat_timespec ct;
		struct timespec t;

		jiffies_to_timespec(expire, &t);
		ct.tv_sec = t.tv_sec;
		ct.tv_nsec = t.tv_nsec;

		ret = -ERESTART_RESTARTBLOCK;
		if (copy_to_user(rmtp, &ct, sizeof(ct)))
			ret = -EFAULT;
		/* The 'restart' block is already filled in */
	}
	return ret;
}

asmlinkage long compat_sys_nanosleep(struct compat_timespec *rqtp,
		struct compat_timespec *rmtp)
{
	struct compat_timespec ct;
	struct timespec t;
	unsigned long expire;
	s32 ret;

	if (copy_from_user(&ct, rqtp, sizeof(ct)))
		return -EFAULT;
	t.tv_sec = ct.tv_sec;
	t.tv_nsec = ct.tv_nsec;

	if ((t.tv_nsec >= 1000000000L) || (t.tv_nsec < 0) || (t.tv_sec < 0))
		return -EINVAL;

	expire = timespec_to_jiffies(&t) + (t.tv_sec || t.tv_nsec);
	current->state = TASK_INTERRUPTIBLE;
	expire = schedule_timeout(expire);

	ret = 0;
	if (expire) {
		struct restart_block *restart;

		jiffies_to_timespec(expire, &t);
		ct.tv_sec = t.tv_sec;
		ct.tv_nsec = t.tv_nsec;

		if (copy_to_user(rmtp, &ct, sizeof(ct)))
			return -EFAULT;

		restart = &current_thread_info()->restart_block;
		restart->fn = compat_nanosleep_restart;
		restart->arg0 = jiffies + expire;
		restart->arg1 = (unsigned long) rmtp;
		ret = -ERESTART_RESTARTBLOCK;
	}
	return ret;
}

/*
 * Not all architectures have sys_utime, so implement this in terms
 * of sys_utimes.
 */
asmlinkage long compat_sys_utime(char *filename, struct compat_utimbuf *t)
{
	struct timeval tv[2];

	if (t) {
		if (get_user(tv[0].tv_sec, &t->actime) ||
		    get_user(tv[1].tv_sec, &t->modtime))
			return -EFAULT;
		tv[0].tv_usec = 0;
		tv[1].tv_usec = 0;
	}
	return do_utimes(filename, t ? tv : NULL);
}


static inline long get_compat_itimerval(struct itimerval *o,
		struct compat_itimerval *i)
{
	return (!access_ok(VERIFY_READ, i, sizeof(*i)) ||
		(__get_user(o->it_interval.tv_sec, &i->it_interval.tv_sec) |
		 __get_user(o->it_interval.tv_usec, &i->it_interval.tv_usec) |
		 __get_user(o->it_value.tv_sec, &i->it_value.tv_sec) |
		 __get_user(o->it_value.tv_usec, &i->it_value.tv_usec)));
}

static inline long put_compat_itimerval(struct compat_itimerval *o,
		struct itimerval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->it_interval.tv_sec, &o->it_interval.tv_sec) |
		 __put_user(i->it_interval.tv_usec, &o->it_interval.tv_usec) |
		 __put_user(i->it_value.tv_sec, &o->it_value.tv_sec) |
		 __put_user(i->it_value.tv_usec, &o->it_value.tv_usec)));
}

extern int do_getitimer(int which, struct itimerval *value);

asmlinkage long compat_sys_getitimer(int which, struct compat_itimerval *it)
{
	struct itimerval kit;
	int error;

	error = do_getitimer(which, &kit);
	if (!error && put_compat_itimerval(it, &kit))
		error = -EFAULT;
	return error;
}

extern int do_setitimer(int which, struct itimerval *, struct itimerval *);

asmlinkage long compat_sys_setitimer(int which, struct compat_itimerval *in,
		struct compat_itimerval *out)
{
	struct itimerval kin, kout;
	int error;

	if (in) {
		if (get_compat_itimerval(&kin, in))
			return -EFAULT;
	} else
		memset(&kin, 0, sizeof(kin));

	error = do_setitimer(which, &kin, out ? &kout : NULL);
	if (error || !out)
		return error;
	if (put_compat_itimerval(out, &kout))
		return -EFAULT;
	return 0;
}
