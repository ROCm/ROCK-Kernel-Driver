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
#include <linux/syscalls.h>
#include <linux/unistd.h>

#include <asm/uaccess.h>

int get_compat_timespec(struct timespec *ts, const struct compat_timespec __user *cts)
{
	return (verify_area(VERIFY_READ, cts, sizeof(*cts)) ||
			__get_user(ts->tv_sec, &cts->tv_sec) ||
			__get_user(ts->tv_nsec, &cts->tv_nsec)) ? -EFAULT : 0;
}

int put_compat_timespec(const struct timespec *ts, struct compat_timespec __user *cts)
{
	return (verify_area(VERIFY_WRITE, cts, sizeof(*cts)) ||
			__put_user(ts->tv_sec, &cts->tv_sec) ||
			__put_user(ts->tv_nsec, &cts->tv_nsec)) ? -EFAULT : 0;
}

static long compat_nanosleep_restart(struct restart_block *restart)
{
	unsigned long expire = restart->arg0, now = jiffies;
	struct compat_timespec __user *rmtp;

	/* Did it expire while we handled signals? */
	if (!time_after(expire, now))
		return 0;

	current->state = TASK_INTERRUPTIBLE;
	expire = schedule_timeout(expire - now);
	if (expire == 0)
		return 0;

	rmtp = (struct compat_timespec __user *)restart->arg1;
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

asmlinkage long compat_sys_nanosleep(struct compat_timespec __user *rqtp,
		struct compat_timespec __user *rmtp)
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
		struct compat_itimerval __user *i)
{
	return (!access_ok(VERIFY_READ, i, sizeof(*i)) ||
		(__get_user(o->it_interval.tv_sec, &i->it_interval.tv_sec) |
		 __get_user(o->it_interval.tv_usec, &i->it_interval.tv_usec) |
		 __get_user(o->it_value.tv_sec, &i->it_value.tv_sec) |
		 __get_user(o->it_value.tv_usec, &i->it_value.tv_usec)));
}

static inline long put_compat_itimerval(struct compat_itimerval __user *o,
		struct itimerval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->it_interval.tv_sec, &o->it_interval.tv_sec) |
		 __put_user(i->it_interval.tv_usec, &o->it_interval.tv_usec) |
		 __put_user(i->it_value.tv_sec, &o->it_value.tv_sec) |
		 __put_user(i->it_value.tv_usec, &o->it_value.tv_usec)));
}

asmlinkage long compat_sys_getitimer(int which,
		struct compat_itimerval __user *it)
{
	struct itimerval kit;
	int error;

	error = do_getitimer(which, &kit);
	if (!error && put_compat_itimerval(it, &kit))
		error = -EFAULT;
	return error;
}

asmlinkage long compat_sys_setitimer(int which,
		struct compat_itimerval __user *in,
		struct compat_itimerval __user *out)
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

asmlinkage long compat_sys_times(struct compat_tms __user *tbuf)
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

asmlinkage long compat_sys_sigpending(compat_old_sigset_t __user *set)
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

asmlinkage long compat_sys_sigprocmask(int how, compat_old_sigset_t __user *set,
		compat_old_sigset_t __user *oset)
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
		if (oset)
			ret = put_user(s, oset);
	return ret;
}

#ifdef CONFIG_FUTEX
asmlinkage long compat_sys_futex(u32 __user *uaddr, int op, int val,
		struct compat_timespec __user *utime, u32 __user *uaddr2,
		int val3)
{
	struct timespec t;
	unsigned long timeout = MAX_SCHEDULE_TIMEOUT;
	int val2 = 0;

	if ((op == FUTEX_WAIT) && utime) {
		if (get_compat_timespec(&t, utime))
			return -EFAULT;
		timeout = timespec_to_jiffies(&t) + 1;
	}
	if (op >= FUTEX_REQUEUE)
		val2 = (int) (long) utime;

	return do_futex((unsigned long)uaddr, op, val, timeout,
			(unsigned long)uaddr2, val2, val3);
}
#endif

asmlinkage long compat_sys_setrlimit(unsigned int resource,
		struct compat_rlimit __user *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs ();

	if (resource >= RLIM_NLIMITS) 
		return -EINVAL;	

	if (!access_ok(VERIFY_READ, rlim, sizeof(*rlim)) ||
	    __get_user(r.rlim_cur, &rlim->rlim_cur) ||
	    __get_user(r.rlim_max, &rlim->rlim_max))
		return -EFAULT;

	if (r.rlim_cur == COMPAT_RLIM_INFINITY)
		r.rlim_cur = RLIM_INFINITY;
	if (r.rlim_max == COMPAT_RLIM_INFINITY)
		r.rlim_max = RLIM_INFINITY;
	set_fs(KERNEL_DS);
	ret = sys_setrlimit(resource, &r);
	set_fs(old_fs);
	return ret;
}

#ifdef COMPAT_RLIM_OLD_INFINITY

asmlinkage long compat_sys_old_getrlimit(unsigned int resource,
		struct compat_rlimit __user *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_old_getrlimit(resource, &r);
	set_fs(old_fs);

	if (!ret) {
		if (r.rlim_cur > COMPAT_RLIM_OLD_INFINITY)
			r.rlim_cur = COMPAT_RLIM_INFINITY;
		if (r.rlim_max > COMPAT_RLIM_OLD_INFINITY)
			r.rlim_max = COMPAT_RLIM_INFINITY;

		if (!access_ok(VERIFY_WRITE, rlim, sizeof(*rlim)) ||
		    __put_user(r.rlim_cur, &rlim->rlim_cur) ||
		    __put_user(r.rlim_max, &rlim->rlim_max))
			return -EFAULT;
	}
	return ret;
}

#endif

asmlinkage long compat_sys_getrlimit (unsigned int resource,
		struct compat_rlimit __user *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_getrlimit(resource, &r);
	set_fs(old_fs);
	if (!ret) {
		if (r.rlim_cur > COMPAT_RLIM_INFINITY)
			r.rlim_cur = COMPAT_RLIM_INFINITY;
		if (r.rlim_max > COMPAT_RLIM_INFINITY)
			r.rlim_max = COMPAT_RLIM_INFINITY;

		if (!access_ok(VERIFY_WRITE, rlim, sizeof(*rlim)) ||
		    __put_user(r.rlim_cur, &rlim->rlim_cur) ||
		    __put_user(r.rlim_max, &rlim->rlim_max))
			return -EFAULT;
	}
	return ret;
}

static long put_compat_rusage(struct compat_rusage __user *ru, struct rusage *r)
{
	if (!access_ok(VERIFY_WRITE, ru, sizeof(*ru)) ||
	    __put_user(r->ru_utime.tv_sec, &ru->ru_utime.tv_sec) ||
	    __put_user(r->ru_utime.tv_usec, &ru->ru_utime.tv_usec) ||
	    __put_user(r->ru_stime.tv_sec, &ru->ru_stime.tv_sec) ||
	    __put_user(r->ru_stime.tv_usec, &ru->ru_stime.tv_usec) ||
	    __put_user(r->ru_maxrss, &ru->ru_maxrss) ||
	    __put_user(r->ru_ixrss, &ru->ru_ixrss) ||
	    __put_user(r->ru_idrss, &ru->ru_idrss) ||
	    __put_user(r->ru_isrss, &ru->ru_isrss) ||
	    __put_user(r->ru_minflt, &ru->ru_minflt) ||
	    __put_user(r->ru_majflt, &ru->ru_majflt) ||
	    __put_user(r->ru_nswap, &ru->ru_nswap) ||
	    __put_user(r->ru_inblock, &ru->ru_inblock) ||
	    __put_user(r->ru_oublock, &ru->ru_oublock) ||
	    __put_user(r->ru_msgsnd, &ru->ru_msgsnd) ||
	    __put_user(r->ru_msgrcv, &ru->ru_msgrcv) ||
	    __put_user(r->ru_nsignals, &ru->ru_nsignals) ||
	    __put_user(r->ru_nvcsw, &ru->ru_nvcsw) ||
	    __put_user(r->ru_nivcsw, &ru->ru_nivcsw))
		return -EFAULT;
	return 0;
}

asmlinkage long compat_sys_getrusage(int who, struct compat_rusage __user *ru)
{
	struct rusage r;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_getrusage(who, &r);
	set_fs(old_fs);

	if (ret)
		return ret;

	if (put_compat_rusage(ru, &r))
		return -EFAULT;

	return 0;
}

asmlinkage long
compat_sys_wait4(compat_pid_t pid, compat_uint_t __user *stat_addr, int options,
	struct compat_rusage __user *ru)
{
	if (!ru) {
		return sys_wait4(pid, stat_addr, options, NULL);
	} else {
		struct rusage r;
		int ret;
		unsigned int status;
		mm_segment_t old_fs = get_fs();

		set_fs (KERNEL_DS);
		ret = sys_wait4(pid, stat_addr ? &status : NULL, options, &r);
		set_fs (old_fs);

		if (ret > 0) {
			if (put_compat_rusage(ru, &r)) 
				return -EFAULT;
			if (stat_addr && put_user(status, stat_addr))
				return -EFAULT;
		}
		return ret;
	}
}

asmlinkage long compat_sys_sched_setaffinity(compat_pid_t pid, 
					     unsigned int len,
					     compat_ulong_t __user *user_mask_ptr)
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

asmlinkage int compat_sys_sched_getaffinity(compat_pid_t pid, unsigned int len,
					   compat_ulong_t __user *user_mask_ptr)
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
		ret = sizeof(compat_ulong_t);
		if (put_user(kernel_mask, user_mask_ptr))
			return -EFAULT;
	}

	return ret;
}

static int get_compat_itimerspec(struct itimerspec *dst, 
				 struct compat_itimerspec __user *src)
{ 
	if (get_compat_timespec(&dst->it_interval, &src->it_interval) ||
	    get_compat_timespec(&dst->it_value, &src->it_value))
		return -EFAULT;
	return 0;
} 

static int put_compat_itimerspec(struct compat_itimerspec __user *dst, 
				 struct itimerspec *src)
{ 
	if (put_compat_timespec(&src->it_interval, &dst->it_interval) ||
	    put_compat_timespec(&src->it_value, &dst->it_value))
		return -EFAULT;
	return 0;
} 

long compat_timer_settime(timer_t timer_id, int flags, 
			  struct compat_itimerspec __user *new, 
			  struct compat_itimerspec __user *old)
{ 
	long err;
	mm_segment_t oldfs;
	struct itimerspec newts, oldts;

	if (!new)
		return -EINVAL;
	if (get_compat_itimerspec(&newts, new))
		return -EFAULT;	
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_timer_settime(timer_id, flags, &newts, &oldts);
	set_fs(oldfs); 
	if (!err && old && put_compat_itimerspec(old, &oldts))
		return -EFAULT;
	return err;
} 

long compat_timer_gettime(timer_t timer_id,
		struct compat_itimerspec __user *setting)
{ 
	long err;
	mm_segment_t oldfs;
	struct itimerspec ts; 
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_timer_gettime(timer_id, &ts); 
	set_fs(oldfs); 
	if (!err && put_compat_itimerspec(setting, &ts))
		return -EFAULT;
	return err;
} 

long compat_clock_settime(clockid_t which_clock,
		struct compat_timespec __user *tp)
{
	long err;
	mm_segment_t oldfs;
	struct timespec ts; 
	if (get_compat_timespec(&ts, tp))
		return -EFAULT; 
	oldfs = get_fs();
	set_fs(KERNEL_DS);	
	err = sys_clock_settime(which_clock, &ts); 
	set_fs(oldfs);
	return err;
} 

long compat_clock_gettime(clockid_t which_clock,
		struct compat_timespec __user *tp)
{
	long err;
	mm_segment_t oldfs;
	struct timespec ts; 
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_clock_gettime(which_clock, &ts); 
	set_fs(oldfs);
	if (!err && put_compat_timespec(&ts, tp))
		return -EFAULT; 
	return err;
} 

long compat_clock_getres(clockid_t which_clock,
		struct compat_timespec __user *tp)
{
	long err;
	mm_segment_t oldfs;
	struct timespec ts; 
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_clock_getres(which_clock, &ts); 
	set_fs(oldfs);
	if (!err && put_compat_timespec(&ts, tp))
		return -EFAULT; 
	return err;
} 

long compat_clock_nanosleep(clockid_t which_clock, int flags,
			    struct compat_timespec __user *rqtp,
			    struct compat_timespec __user *rmtp)
{
	long err;
	mm_segment_t oldfs;
	struct timespec in, out; 
	if (get_compat_timespec(&in, rqtp)) 
		return -EFAULT;
	oldfs = get_fs();
	set_fs(KERNEL_DS);	
	err = sys_clock_nanosleep(which_clock, flags, &in, &out);  
	set_fs(oldfs);
	if ((err == -ERESTART_RESTARTBLOCK) && rmtp &&
	    put_compat_timespec(&out, rmtp))
		return -EFAULT;
	return err;	
} 

/* timer_create is architecture specific because it needs sigevent conversion */

