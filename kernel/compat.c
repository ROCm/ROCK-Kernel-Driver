/*
 *  linux/kernel/compat.c
 *
 *  Kernel compatibililty routines for e.g. 32 bit syscall support
 *  on 64 bit kernels.
 *
 *  Copyright (C) 2002-2003 Stephen Rothwell, IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/signal.h>
#include <linux/sched.h>	/* for MAX_SCHEDULE_TIMEOUT */
#include <linux/futex.h>	/* for FUTEX_WAIT */

#include <asm/uaccess.h>

int get_compat_timespec(struct timespec *ts, struct compat_timespec *cts)
{
	return (verify_area(VERIFY_READ, cts, sizeof(*cts)) ||
			__get_user(ts->tv_sec, &cts->tv_sec) ||
			__get_user(ts->tv_nsec, &cts->tv_nsec)) ? -EFAULT : 0;
}

int put_compat_timespec(struct timespec *ts, struct compat_timespec *cts)
{
	return (verify_area(VERIFY_WRITE, cts, sizeof(*cts)) ||
			__put_user(ts->tv_sec, &cts->tv_sec) ||
			__put_user(ts->tv_nsec, &cts->tv_nsec)) ? -EFAULT : 0;
}

static long compat_nanosleep_restart(struct restart_block *restart)
{
	unsigned long expire = restart->arg0, now = jiffies;
	struct compat_timespec *rmtp;

	/* Did it expire while we handled signals? */
	if (!time_after(expire, now))
		return 0;

	current->state = TASK_INTERRUPTIBLE;
	expire = schedule_timeout(expire - now);
	if (expire == 0)
		return 0;

	rmtp = (struct compat_timespec *)restart->arg1;
	if (rmtp) {
		struct compat_timespec ct;
		struct timespec t;

		jiffies_to_timespec(expire, &t);
		ct.tv_sec = t.tv_sec;
		ct.tv_nsec = t.tv_nsec;
		if (copy_to_user(rmtp, &ct, sizeof(ct)))
			return -EFAULT;
	}
	/* The 'restart' block is already filled in */
	return -ERESTART_RESTARTBLOCK;
}

asmlinkage long compat_sys_nanosleep(struct compat_timespec *rqtp,
		struct compat_timespec *rmtp)
{
	struct timespec t;
	struct restart_block *restart;
	unsigned long expire;

	if (get_compat_timespec(&t, rqtp))
		return -EFAULT;

	if ((t.tv_nsec >= 1000000000L) || (t.tv_nsec < 0) || (t.tv_sec < 0))
		return -EINVAL;

	expire = timespec_to_jiffies(&t) + (t.tv_sec || t.tv_nsec);
	current->state = TASK_INTERRUPTIBLE;
	expire = schedule_timeout(expire);
	if (expire == 0)
		return 0;

	if (rmtp) {
		jiffies_to_timespec(expire, &t);
		if (put_compat_timespec(&t, rmtp))
			return -EFAULT;
	}
	restart = &current_thread_info()->restart_block;
	restart->fn = compat_nanosleep_restart;
	restart->arg0 = jiffies + expire;
	restart->arg1 = (unsigned long) rmtp;
	return -ERESTART_RESTARTBLOCK;
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

asmlinkage long compat_sys_times(struct compat_tms *tbuf)
{
	/*
	 *	In the SMP world we might just be unlucky and have one of
	 *	the times increment as we use it. Since the value is an
	 *	atomically safe type this is just fine. Conceptually its
	 *	as if the syscall took an instant longer to occur.
	 */
	if (tbuf) {
		struct compat_tms tmp;
		tmp.tms_utime = compat_jiffies_to_clock_t(current->utime);
		tmp.tms_stime = compat_jiffies_to_clock_t(current->stime);
		tmp.tms_cutime = compat_jiffies_to_clock_t(current->cutime);
		tmp.tms_cstime = compat_jiffies_to_clock_t(current->cstime);
		if (copy_to_user(tbuf, &tmp, sizeof(tmp)))
			return -EFAULT;
	}
	return compat_jiffies_to_clock_t(jiffies);
}

/*
 * Assumption: old_sigset_t and compat_old_sigset_t are both
 * types that can be passed to put_user()/get_user().
 */

extern asmlinkage long sys_sigpending(old_sigset_t *);

asmlinkage long compat_sys_sigpending(compat_old_sigset_t *set)
{
	old_sigset_t s;
	long ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_sigpending(&s);
	set_fs(old_fs);
	if (ret == 0)
		ret = put_user(s, set);
	return ret;
}

extern asmlinkage long sys_sigprocmask(int, old_sigset_t *, old_sigset_t *);

asmlinkage long compat_sys_sigprocmask(int how, compat_old_sigset_t *set,
		compat_old_sigset_t *oset)
{
	old_sigset_t s;
	long ret;
	mm_segment_t old_fs;

	if (set && get_user(s, set))
		return -EFAULT;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_sigprocmask(how, set ? &s : NULL, oset ? &s : NULL);
	set_fs(old_fs);
	if (ret == 0)
		ret = put_user(s, oset);
	return ret;
}

extern long do_futex(unsigned long, int, int, unsigned long);

asmlinkage long compat_sys_futex(u32 *uaddr, int op, int val,
		struct compat_timespec *utime)
{
	struct timespec t;
	unsigned long timeout = MAX_SCHEDULE_TIMEOUT;

	if ((op == FUTEX_WAIT) && utime) {
		if (get_compat_timespec(&t, utime))
			return -EFAULT;
		timeout = timespec_to_jiffies(&t) + 1;
	}
	return do_futex((unsigned long)uaddr, op, val, timeout);
}

extern asmlinkage long sys_sched_setaffinity(pid_t pid, unsigned int len,
					    unsigned long *user_mask_ptr);

asmlinkage long compat_sys_sched_setaffinity(compat_pid_t pid, 
					     unsigned int len,
					     compat_ulong_t *user_mask_ptr)
{
	unsigned long kernel_mask;
	mm_segment_t old_fs;
	int ret;

	if (get_user(kernel_mask, user_mask_ptr))
		return -EFAULT;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_sched_setaffinity(pid,
				    sizeof(kernel_mask),
				    &kernel_mask);
	set_fs(old_fs);

	return ret;
}

extern asmlinkage long sys_sched_getaffinity(pid_t pid, unsigned int len,
					    unsigned long *user_mask_ptr);

asmlinkage int compat_sys_sched_getaffinity(compat_pid_t pid, unsigned int len,
					    compat_ulong_t *user_mask_ptr)
{
	unsigned long kernel_mask;
	mm_segment_t old_fs;
	int ret;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_sched_getaffinity(pid,
				    sizeof(kernel_mask),
				    &kernel_mask);
	set_fs(old_fs);

	if (ret > 0) {
		if (put_user(kernel_mask, user_mask_ptr))
			ret = -EFAULT;
		ret = sizeof(compat_ulong_t);
	}

	return ret;
}

