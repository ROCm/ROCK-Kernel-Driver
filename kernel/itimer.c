/*
 * linux/kernel/itimer.c
 *
 * Copyright (C) 1992 Darren Senn
 */

/* These are all the functions necessary to implement itimers */

#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/time.h>

#include <asm/uaccess.h>

int do_getitimer(int which, struct itimerval *value)
{
	register unsigned long val, interval;

	switch (which) {
	case ITIMER_REAL:
		interval = current->it_real_incr;
		val = 0;
		/* 
		 * FIXME! This needs to be atomic, in case the kernel timer happens!
		 */
		if (timer_pending(&current->real_timer)) {
			val = current->real_timer.expires - jiffies;

			/* look out for negative/zero itimer.. */
			if ((long) val <= 0)
				val = 1;
		}
		break;
	case ITIMER_VIRTUAL:
		val = current->it_virt_value;
		interval = current->it_virt_incr;
		break;
	case ITIMER_PROF:
		val = current->it_prof_value;
		interval = current->it_prof_incr;
		break;
	default:
		return(-EINVAL);
	}
	jiffies_to_timeval(val, &value->it_value);
	jiffies_to_timeval(interval, &value->it_interval);
	return 0;
}

/* SMP: Only we modify our itimer values. */
asmlinkage long sys_getitimer(int which, struct itimerval __user *value)
{
	int error = -EFAULT;
	struct itimerval get_buffer;

	if (value) {
		error = do_getitimer(which, &get_buffer);
		if (!error &&
		    copy_to_user(value, &get_buffer, sizeof(get_buffer)))
			error = -EFAULT;
	}
	return error;
}

void it_real_fn(unsigned long __data)
{
	struct task_struct * p = (struct task_struct *) __data;
	unsigned long interval;

	send_group_sig_info(SIGALRM, SEND_SIG_PRIV, p);
	interval = p->it_real_incr;
	if (interval) {
		if (interval > (unsigned long) LONG_MAX)
			interval = LONG_MAX;
		p->real_timer.expires = jiffies + interval;
		add_timer(&p->real_timer);
	}
}

int do_setitimer(int which, struct itimerval *value, struct itimerval *ovalue)
{
	register unsigned long i, j;
	int k;

	i = timeval_to_jiffies(&value->it_interval);
	j = timeval_to_jiffies(&value->it_value);
	if (ovalue && (k = do_getitimer(which, ovalue)) < 0)
		return k;
	switch (which) {
		case ITIMER_REAL:
			del_timer_sync(&current->real_timer);
			current->it_real_value = j;
			current->it_real_incr = i;
			if (!j)
				break;
			if (j > (unsigned long) LONG_MAX)
				j = LONG_MAX;
			i = j + jiffies;
			current->real_timer.expires = i;
			add_timer(&current->real_timer);
			break;
		case ITIMER_VIRTUAL:
			if (j)
				j++;
			current->it_virt_value = j;
			current->it_virt_incr = i;
			break;
		case ITIMER_PROF:
			if (j)
				j++;
			current->it_prof_value = j;
			current->it_prof_incr = i;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

/* SMP: Again, only we play with our itimers, and signals are SMP safe
 *      now so that is not an issue at all anymore.
 */
asmlinkage long sys_setitimer(int which,
			      struct itimerval __user *value,
			      struct itimerval __user *ovalue)
{
	struct itimerval set_buffer, get_buffer;
	int error;

	if (value) {
		if(copy_from_user(&set_buffer, value, sizeof(set_buffer)))
			return -EFAULT;
	} else
		memset((char *) &set_buffer, 0, sizeof(set_buffer));

	error = do_setitimer(which, &set_buffer, ovalue ? &get_buffer : 0);
	if (error || !ovalue)
		return error;

	if (copy_to_user(ovalue, &get_buffer, sizeof(get_buffer)))
		return -EFAULT; 
	return 0;
}
