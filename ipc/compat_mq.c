/*
 *  ipc/compat_mq.c
 *    32 bit emulation for POSIX message queue system calls
 *
 *    Copyright (C) 2004 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author: Arnd Bergmann <arnd@arndb.de>
 */

#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mqueue.h>
#include <linux/syscalls.h>

#include <asm/uaccess.h>

struct compat_mq_attr {
	compat_long_t mq_flags;      /* message queue flags		     */
	compat_long_t mq_maxmsg;     /* maximum number of messages	     */
	compat_long_t mq_msgsize;    /* maximum message size		     */
	compat_long_t mq_curmsgs;    /* number of messages currently queued  */
	compat_long_t __reserved[4]; /* ignored for input, zeroed for output */
};

static inline int get_compat_mq_attr(struct mq_attr *attr,
			const struct compat_mq_attr __user *uattr)
{
	if (verify_area(VERIFY_READ, uattr, sizeof *uattr))
		return -EFAULT;

	return __get_user(attr->mq_flags, &uattr->mq_flags)
		| __get_user(attr->mq_maxmsg, &uattr->mq_maxmsg)
		| __get_user(attr->mq_msgsize, &uattr->mq_msgsize)
		| __get_user(attr->mq_curmsgs, &uattr->mq_curmsgs);
}

static inline int put_compat_mq_attr(const struct mq_attr *attr,
			struct compat_mq_attr __user *uattr)
{
	if (clear_user(uattr, sizeof *uattr))
		return -EFAULT;

	return __put_user(attr->mq_flags, &uattr->mq_flags)
		| __put_user(attr->mq_maxmsg, &uattr->mq_maxmsg)
		| __put_user(attr->mq_msgsize, &uattr->mq_msgsize)
		| __put_user(attr->mq_curmsgs, &uattr->mq_curmsgs);
}

asmlinkage long compat_sys_mq_open(const char __user *u_name,
			int oflag, compat_mode_t mode,
			struct compat_mq_attr __user *u_attr)
{
	struct mq_attr attr;
	mm_segment_t oldfs;
	char *name;
	long ret;

	if ((oflag & O_CREAT) == 0 || !u_attr)
		return sys_mq_open(u_name, oflag, mode, 0);

	if (get_compat_mq_attr(&attr, u_attr))
		return -EFAULT;

	name = getname(u_name);
	if (IS_ERR(name))
		return PTR_ERR(name);

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_mq_open(name, oflag, mode, &attr);
	set_fs(oldfs);

	putname(name);
	return ret;
}

static struct timespec __user *compat_prepare_timeout(
			const struct compat_timespec __user *u_abs_timeout)
{
	struct timespec ts;
	struct timespec __user *u_ts;

	if (!u_abs_timeout)
		return 0;

	u_ts = compat_alloc_user_space(sizeof(*u_ts));
	if (get_compat_timespec(&ts, u_abs_timeout)
		|| copy_to_user(u_ts, &ts, sizeof(*u_ts)))
		return ERR_PTR(-EFAULT);

	return u_ts;
}

asmlinkage long compat_sys_mq_timedsend(mqd_t mqdes,
			const char __user *u_msg_ptr,
			size_t msg_len, unsigned int msg_prio,
			const struct compat_timespec __user *u_abs_timeout)
{
	struct timespec __user *u_ts;

	u_ts = compat_prepare_timeout(u_abs_timeout);
	if (IS_ERR(u_ts))
		return -EFAULT;

	return sys_mq_timedsend(mqdes, u_msg_ptr, msg_len,
			msg_prio, u_ts);
}

asmlinkage ssize_t compat_sys_mq_timedreceive(mqd_t mqdes,
			char __user *u_msg_ptr,
			size_t msg_len, unsigned int __user *u_msg_prio,
			const struct compat_timespec __user *u_abs_timeout)
{
	struct timespec __user *u_ts;

	u_ts = compat_prepare_timeout(u_abs_timeout);
	if (IS_ERR(u_ts))
		return -EFAULT;

	return sys_mq_timedreceive(mqdes, u_msg_ptr, msg_len,
			u_msg_prio, u_ts);
}

static int get_compat_sigevent(struct sigevent *event,
		const struct compat_sigevent __user *u_event)
{
	if (verify_area(VERIFY_READ, u_event, sizeof(*u_event)))
		return -EFAULT;

	return __get_user(event->sigev_value.sival_int,
			  &u_event->sigev_value.sival_int)
	     | __get_user(event->sigev_signo, &u_event->sigev_signo)
	     | __get_user(event->sigev_notify, &u_event->sigev_notify)
	     | __get_user(event->sigev_notify_thread_id,
			  &u_event->sigev_notify_thread_id);
}

asmlinkage long compat_sys_mq_notify(mqd_t mqdes,
			const struct compat_sigevent __user *u_notification)
{
	mm_segment_t oldfs;
	struct sigevent notification;
	char cookie[NOTIFY_COOKIE_LEN];
	compat_uptr_t u_cookie;
	long ret;

	if (!u_notification)
		return sys_mq_notify(mqdes, 0);

	if (get_compat_sigevent(&notification, u_notification))
		return -EFAULT;

	if (notification.sigev_notify == SIGEV_THREAD) {
		u_cookie = (compat_uptr_t)notification.sigev_value.sival_int;
		if (copy_from_user(cookie, compat_ptr(u_cookie),
						NOTIFY_COOKIE_LEN)) {
			return -EFAULT;
		}
		notification.sigev_value.sival_ptr = cookie;
	}

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_mq_notify(mqdes, &notification);
	set_fs(oldfs);

	return ret;
}

asmlinkage long compat_sys_mq_getsetattr(mqd_t mqdes,
			const struct compat_mq_attr __user *u_mqstat,
			struct compat_mq_attr __user *u_omqstat)
{
	struct mq_attr mqstat, omqstat;
	struct mq_attr *p_mqstat = 0, *p_omqstat = 0;
	mm_segment_t oldfs;
	long ret;

	if (u_mqstat) {
		p_mqstat = &mqstat;
		if (get_compat_mq_attr(p_mqstat, u_mqstat))
			return -EFAULT;
	}

	if (u_omqstat)
		p_omqstat = &omqstat;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_mq_getsetattr(mqdes, p_mqstat, p_omqstat);
	set_fs(oldfs);

	if (ret)
		return ret;

	return (u_omqstat) ? put_compat_mq_attr(&omqstat, u_omqstat) : 0;
}
