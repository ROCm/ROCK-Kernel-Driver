/*
 * control.c
 *
 * Linux Audit Subsystem
 *
 * Copyright (C) 2003 SuSE Linux AG
 *
 * Written by okir@suse.de, based on ideas from systrace, by
 * Niels Provos (OpenBSD) and ported to Linux by Marius Aamodt Eriksen.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "audit-private.h"

#include <linux/version.h>
#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/sys.h>
#include <linux/miscdevice.h>
#include <linux/personality.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>

#define AUDIT_VERSION		"0.2"
#define AUDIT_MINOR		224

static unsigned int		audit_id;
static struct aud_policy	audit_policy[__AUD_MAX_POLICY];

static DECLARE_RWSEM(audit_lock);

/* These are accessible through sysctl */
int				audit_debug = 0;
int				audit_all_processes = 0;
int				audit_allow_suspend = 1;
int				audit_paranoia = 0;

static int	__audit_attach(struct task_struct *, int, struct aud_process *);
static void	audit_attach_all(void);
static void	audit_detach_all(void);

static struct file_operations audit_fops = {
	.owner =   THIS_MODULE,
	.read =    &auditf_read,
	.write =   &auditf_write,
	.ioctl =   &auditf_ioctl,
	.release = &auditf_release,
	.open =    &auditf_open,
	.poll =    &auditf_poll
};

static struct miscdevice audit_dev = {
	AUDIT_MINOR,
	"audit",
	&audit_fops
};


#ifdef MODULE
static int	__audit_vintercept(enum audit_call, va_list);
static void	__audit_fork(struct task_struct *, struct task_struct *);
static void	__audit_exit(struct task_struct *, long code);
static void	__audit_netlink_msg(struct sk_buff *, int);

#define audit_exit	__audit_exit
#define audit_fork	__audit_fork
#define audit_netlink_msg __audit_netlink_msg

static const struct audit_hooks audit_hooks = {
	__audit_vintercept,
	audit_lresult,
	__audit_fork,
	__audit_exit,
	__audit_netlink_msg,
};
#endif


static int __init
init_audit(void)
{
	audit_init_syscalls();

	if (misc_register(&audit_dev) < 0) {
		printk(KERN_INFO "audit: unable to register device\n");
		return -EIO;
	}

	if (audit_sysctl_register() < 0)
		goto fail_unregister;

	if (audit_register_ioctl_converters() < 0)
		goto fail_unregister;

#ifdef MODULE
	if (audit_register(&audit_hooks) < 0)
		goto fail_unregister;
#endif

	printk(KERN_INFO "audit subsystem ver %s initialized\n",
		AUDIT_VERSION);

	return 0;

fail_unregister:
	(void)audit_unregister_ioctl_converters();
	audit_sysctl_unregister();
	misc_deregister(&audit_dev);
	return -EIO;
}

module_init(init_audit);

#ifdef MODULE

static void __exit 
exit_audit(void)
{
	/* Detach all audited processes */
	audit_detach_all();

	audit_unregister();

	(void)audit_unregister_ioctl_converters();
	audit_sysctl_unregister();
	misc_deregister(&audit_dev);

	audit_policy_clear();
	audit_filter_clear();
}

module_exit(exit_audit);

#endif

int
auditf_open(struct inode *inode, struct file *file)
{
	struct aud_context *ctx;
	int error = 0;

	DPRINTF("opened by pid %d\n", current->pid);
	if ((ctx = kmalloc(sizeof(*ctx), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "audit: Failed to allocate kernel memory.\n");
		return -ENOBUFS;
	}

	memset(ctx, 0, sizeof(*ctx));
	file->private_data = ctx;

	return (error);
}

int
auditf_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct aud_context *ctx = (struct aud_context *) file->private_data;
	int error = 0;

	if (cmd == AUIOCVERSION)
		return AUDIT_API_VERSION;

	DPRINTF("ctx=%p, cmd=0x%x\n", ctx, cmd);
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (cmd) {
	case AUIOCIAMAUDITD:
		down_write(&audit_lock);
		error = audit_msg_enable();
		if (error >= 0) {
			printk(KERN_NOTICE
				"Audit daemon registered (process %d)\n",
				current->pid);
			/* Suspend auditing for this process */
			if (current->audit)
				((struct aud_process *) current->audit)->suspended++;
			ctx->reader = 1;
		}
		if (audit_all_processes)
			audit_attach_all();
		up_write(&audit_lock);
		break;
	case AUIOCATTACH:
		down_write(&audit_lock);
		/* Attach process. If we're the audit daemon,
		 * suspend auditing for us. */
		error = audit_attach(ctx->reader);
		up_write(&audit_lock);
		break;
	case AUIOCDETACH:
		down_write(&audit_lock);
		error = audit_detach();
		up_write(&audit_lock);
		break;
	case AUIOCSUSPEND:
		down_write(&audit_lock);
		error = audit_suspend();
		up_write(&audit_lock);
		break;
	case AUIOCRESUME:
		down_write(&audit_lock);
		error = audit_resume();
		up_write(&audit_lock);
		break;
	case AUIOCCLRPOLICY:
		down_write(&audit_lock);
		error = audit_policy_clear();
		up_write(&audit_lock);
		break;
	case AUIOCCLRFILTER:
		down_write(&audit_lock);
		error = audit_filter_clear();
		up_write(&audit_lock);
		break;
	case AUIOCSETFILTER:
		down_write(&audit_lock);
		error = audit_filter_add((const void __user *) arg);
		up_write(&audit_lock);
		break;
	case AUIOCSETPOLICY:
		down_write(&audit_lock);
		error = audit_policy_set((const void __user *) arg);
		up_write(&audit_lock);
		break;
	case AUIOCSETAUDITID:
		down_write(&audit_lock);
		error = audit_setauditid();
		up_write(&audit_lock);
		break;
	case AUIOCLOGIN:
		down_read(&audit_lock);
		error = audit_login((const void __user *) arg);
		up_read(&audit_lock);
		break;
	case AUIOCUSERMESSAGE:
		down_read(&audit_lock);
		error = audit_user_message((const void __user *) arg);
		up_read(&audit_lock);
		break;
	case AUIOCRESET:
		down_write(&audit_lock);
		audit_detach_all();
		error = audit_policy_clear();
		if (error >= 0)
			error = audit_filter_clear();
		up_write(&audit_lock);
		break;

	default:
		error = -EINVAL;
		break;
	}

	DPRINTF("done, result=%d\n", error);
	return (error);
}

unsigned int
auditf_poll(struct file *file, struct poll_table_struct *wait)
{
	if (audit_msg_poll(file, wait))
		return POLLIN | POLLRDNORM;
	return 0;
}

/*
 * Compute statistics
 */
ssize_t
auditf_read(struct file *filp, char *buf, size_t count, loff_t *off)
{
	struct aud_context *ctx = (struct aud_context *) filp->private_data;
	size_t max_len, copied;
	int block, nmsgs;

	DPRINTF("called.\n");
	if (!ctx->reader)
		return -EPERM;

	/* Get messages from the message queue.
	 * The first time around, extract the first message, no
	 * matter its size.
	 * For subsequent messages, make sure it fits into the buffer.
	 */
	block = !(filp->f_flags & O_NONBLOCK);
	max_len = copied = 0;
	nmsgs = 0;

	while (copied < count) {
		struct aud_msg_head * msgh = audit_msg_get(block, max_len);

		if (IS_ERR(msgh)) {
			if (copied)
				break;
			return PTR_ERR(msgh);
		}

		if (msgh->body.msg_size > count - copied) {
			printk(KERN_NOTICE "auditf_read: truncated audit message (%zu > %zu; max_len=%zu)\n",
				msgh->body.msg_size, count - copied, max_len);
			msgh->body.msg_size = count - copied;
		}

		if (audit_debug > 1) {
			DPRINTF("copying msg %u type %d size %zu\n",
				msgh->body.msg_seqnr, msgh->body.msg_type, msgh->body.msg_size);
		}
		if (copy_to_user(buf + copied, &msgh->body, msgh->body.msg_size)) {
			printk(KERN_ERR "Dropped audit message when copying to audit daemon\n");
			audit_msg_release(msgh);
			return -EFAULT;
		}
		copied += msgh->body.msg_size;
		audit_msg_release(msgh);

		block = 0;
		max_len = count - copied;
		nmsgs++;
	}

	DPRINTF("copied %d messages, %zu bytes total\n", nmsgs, copied);
	return copied;
}

ssize_t
auditf_write(struct file *filp, const char *buf, size_t count, loff_t *off)
{
	return (-ENOTSUPP);
}

int
auditf_release(struct inode *inode, struct file *filp)
{
	struct aud_context *ctx = filp->private_data;

	DPRINTF("called.\n");

	if (ctx->reader) {
		struct aud_msg_head	*msgh;

		DPRINTF("Audit daemon closed audit file; auditing disabled\n");
		audit_msg_disable();

		/* Drop all messages already queued */
		while (1) {
			msgh = audit_msg_get(0, 0);
			if (IS_ERR(msgh))
				break;
			audit_msg_release(msgh);
		}

		/* When we announced being auditd, our
		 * suspend count was bumped */
		audit_resume();
	}

	filp->private_data = NULL;
	kfree(ctx);

	return (0);
}

/*
 * Process intercepted system call and result
 */
static void
__audit_syscall_return(struct aud_process *pinfo, long result)
{
	struct aud_event_data	ev;
	struct aud_syscall_data *sc = &pinfo->syscall;
	int			action, error;

	/* System call ignored, or not supported */
	if (sc->entry == NULL)
		return;

	sc->result = result;

	memset(&ev, 0, sizeof(ev));
	ev.syscall = sc;
	action = audit_policy_check(sc->major, &ev);

	if ((action & AUDIT_LOG) && audit_message_enabled) {
		sc->flags = action;
		error = audit_msg_syscall(pinfo, ev.name, sc);
		/* ENODEV means the audit daemon has gone away.
		 * continue as if we weren't auditing */
		if (error < 0 && error != -ENODEV) {
			printk("audit: error %d when processing syscall %d\n",
					error, sc->major);
		}
	}

	/* If we copied any system call arguments to user
	 * space, release them now.
	 */
	audit_release_arguments(pinfo);

	/* For now, we always invalidate the fileset's cached
	 * dentry pointers.
	 * We could optimize this (e.g. open(2) without O_CREAT
	 * does not change the file system)
	 */
	audit_fileset_unlock(1);

	memset(sc, 0, sizeof(*sc));
}


/*
 * This function is executed in the context of the parent
 * process, with the child process still sleeping
 */
void
audit_fork(struct task_struct *parent, struct task_struct *child)
{
	struct aud_process *parent_info;

	DPRINTF("called.\n");

	task_lock(parent);
	if ((parent_info = parent->audit) != NULL) {
		if (__audit_attach(child, 0, parent_info) == 0) {
			struct aud_process *pinfo = child->audit;

			pinfo->audit_id = parent_info->audit_id;
			pinfo->audit_uid = parent_info->audit_uid;
		}
		else
			printk(KERN_ERR "audit: failed to enable auditing for child process!\n");
	}
	task_unlock(parent);
}

void
audit_exit(struct task_struct *p, long code)
{
	struct aud_process *pinfo;
	int		action;

	/* Notify auditd that we're gone */
	if ((pinfo = p->audit) != NULL) {
		DPRINTF("process exiting, code=%ld\n", code);
		if (!pinfo->suspended) {
			struct aud_event_data	ev;

			__audit_syscall_return(pinfo, 0);

			memset(&ev, 0, sizeof(ev));
			ev.exit_status = code;
			action = audit_policy_check(AUD_POLICY_EXIT, &ev);
			if (action & AUDIT_LOG)
				audit_msg_exit(pinfo, ev.name, code);
		}
		audit_detach();
	}
}

/*
 * Intercept system call
 */
void
audit_intercept(enum audit_call code, ...)
{
	va_list varg;
#ifdef MODULE
	int error;

	va_start(varg, code);
	error = __audit_vintercept(code, varg);
	if (unlikely(error < 0))
		audit_kill_process(error);
}

int
__audit_vintercept(enum audit_call code, va_list varg)
{
#define return(retval) return retval
#else
#define return(retval) return
#endif
	struct aud_syscall_data *sc;
	struct aud_process *pinfo;
	int error;

	/* process attached? */
	if ((pinfo = current->audit) == NULL)
		return(0);

	/* Check if we have system call data we haven't processed
	 * yet, in case there was no call to audit_result.
	 * This happens e.g. for execve(). */
	//todo This breaks for recursive system call invocations.
	__audit_syscall_return(pinfo, 0);

	if (!(pinfo->flags & AUD_F_ATTACHED) || pinfo->suspended || !audit_message_enabled)
		return(0);

	/* Don't dig any deeper if we're not interested in this call */
	if (audit_policy_ignore(code & ~AUDIT_32))
		return(0);

	sc = &pinfo->syscall;
	sc->arch = !(code & AUDIT_32) ? AUDIT_ARCH : AUDIT_ARCH32;
	code &= ~AUDIT_32;
	sc->personality = personality(current->personality);
#ifndef MODULE
	va_start(varg, code);
#endif
	error = audit_get_args(code, varg, sc);
	va_end(varg);

#if 0//todo remove or replace by copy-now-and-verify-unchanged-after-call logic
	/* This doesn't work anymore when not sitting directly in the
	   system call path, because there is no way to replace the
	   arguments passed to the system call. */
	/* Raw, unoptimized -
	 *
	 * We need to protect against two-man con games here,
	 * where one thread enters audit_intercept with say
	 * a pathname of "/ftc/bar", which we don't audit, and
	 * a second thread modifies that to "/etc/bar" before
	 * we actually call the real syscall.
	 *
	 * This is where the "auditing by system call intercept"
	 * concept breaks down quite badly; but that is the price
	 * you pay for an unintrusive patch.
	 */
	if (error >= 0 && audit_paranoia) {
		switch (code) {
		/* While we list the dangerous calls here, most of them do not
		   actually have arguments that still live in user land when the
		   call reaches us. */
		case AUDIT_shmat:
		case AUDIT_shmdt:
		case AUDIT_mmap:
		case AUDIT_munmap:
		case AUDIT_mremap:
		case AUDIT_mprotect:
		case AUDIT_io_setup:
		case AUDIT_madvise:
		case AUDIT_mlock:
		case AUDIT_mlockall:
		case AUDIT_munlock:
		case AUDIT_execve:
			/* These calls mess with the process VM.
			 * Make sure no other thread sharing this VM is
			 * doing any audited call at this time. */
			error = audit_lock_arguments(sc, AUD_F_VM_LOCKED_W);
 			break;
		default:
			error = audit_lock_arguments(sc, AUD_F_VM_LOCKED_R);
			break;
		}
	}
#endif
	if (error >= 0) {
		/* For some system calls, we need to copy one or more arguments
		 * before the call itself:
		 * execve	Never returns, and by the time we get around to
		 *		assembling the audit message, the process image
		 *		is gone.
		 * unlink	Resolve pathnames before file is gone
		 * rename	First pathname needs to be resolved before
		 *		the call; afterwards it's gone already.
		 * chroot	Pathname must be interpreted relative to
		 *		original fs->root.
		 * adjtimex	argument is in/out.
		 */
		switch (code) {
			void *p;

		case AUDIT_adjtimex:
		case AUDIT_execve:
		case AUDIT_unlink:
		case AUDIT_chroot:
			error = audit_copy_arguments(sc);
			break;
		case AUDIT_rename:
			p = audit_get_argument(sc, 0);
			if (IS_ERR(p)) error = PTR_ERR(p);
			break;
		default:
			break;
		}
	}

	if (unlikely(error < 0)) {
		/* An error occurred while copying arguments from user
		 * space. This could either be a simple address fault
		 * (which is alright in some cases, and should just elicit
		 * an error), or it's an internal problem of the audit
		 * subsystem.
		 */
		/* For now, we choose to kill the task. If audit is a module,
		 * we return and let the stub handler do this (because we
		 * need to release the stub lock first)
		 */
#ifndef MODULE
		audit_kill_process(error);
		/* NOTREACHED */
#endif
	}
	return(error);
#undef return
}

/*
 * Intercept system call result
 */
long
audit_lresult(long result)
{
	struct aud_process *pinfo;

	if (!audit_message_enabled)
		return result;

	if ((pinfo = current->audit) == NULL)
		return result;

	/* report return value to audit daemon */
	__audit_syscall_return(pinfo, result);

	return result;
}

/*
 * Netlink message - probably network configuration change
 */
void
audit_netlink_msg(struct sk_buff *skb, int res)
{
	struct nlmsghdr		*nlh;
	struct aud_event_data	ev;
	struct aud_process	*pinfo;
	int			action;

	DPRINTF("called.\n");

	if (!audit_message_enabled)
		return;

	/* Ignore netlink replies for now */
	nlh = (struct nlmsghdr *) skb->data;
	if (!(nlh->nlmsg_flags & NLM_F_REQUEST))
		return;

	if (!(pinfo = current->audit) || pinfo->suspended)
		return;

	memset(&ev, 0, sizeof(ev));
	ev.netconf = skb;

	action = audit_policy_check(AUD_POLICY_NETLINK, &ev);

	if (action & AUDIT_LOG)
		audit_msg_netlink(pinfo, ev.name, skb, res);
}

/*
 * Clear the audit policy table.
 * We hold the audit_lock when we get here.
 */
int
audit_policy_clear(void)
{
	struct aud_policy *policy = audit_policy;
	unsigned i;

	for (i = 0; i < __AUD_MAX_POLICY; i++, policy++) {
		audit_filter_put(policy->filter);
		policy->action = AUDIT_IGNORE;
		policy->filter = NULL;
		if (i < audit_NUM_CALLS)
			audit_fshook_adjust(i, -1);
	}
	return 0;
}

/*
 * Set an audit policy
 * We hold the audit_lock when we get here.
 */
int
audit_policy_set(const struct audit_policy __user *arg)
{
	struct aud_policy *policy;
	struct audit_policy	pol;
	struct aud_filter *f = NULL;

	if (!arg)
		return -EINVAL;
	if (copy_from_user(&pol, arg, sizeof(pol)))
		return -EFAULT;
	DPRINTF("code %u, action %u, filter %u\n", pol.code, pol.action, pol.filter);
	if (pol.code >= __AUD_MAX_POLICY)
		return -EINVAL;

	if (pol.filter > 0 && !(f = audit_filter_get(pol.filter)))
		return -EINVAL;

	policy = audit_policy + pol.code;
	audit_filter_put(policy->filter);
	if (pol.code < audit_NUM_CALLS) {
		audit_fshook_adjust(pol.code,
			(pol.action != AUDIT_IGNORE || f) - (policy->action != AUDIT_IGNORE || policy->filter));
	}
	policy->action = pol.action;
	policy->filter = f;
	return 0;
}

/*
 * Check whether we ignore this system call.
 * Called to find out whether we should bother with
 * decoding arguments etc.
 */
int
audit_policy_ignore(int code)
{
	int result = 1;

	if (0 <= code && code < __AUD_MAX_POLICY) {
		const struct aud_policy *policy = audit_policy + code;

		down_read(&audit_lock);
		if (policy->filter
		 || policy->action != AUDIT_IGNORE)
			result = 0;

		up_read(&audit_lock);
	}

	return result;
}

/*
 * Check policy
 */
static int
__audit_policy_check(int code, struct aud_event_data *ev)
{
	int result = AUDIT_IGNORE;

	if (0 <= code && code < __AUD_MAX_POLICY) {
		const struct aud_policy *policy = audit_policy + code;
		if (policy->filter)
			result = audit_filter_eval(policy->filter, ev);
		else
			result = policy->action;
	}

	return result;
}

int
audit_policy_check(int code, struct aud_event_data *ev)
{
	int result;

	down_read(&audit_lock);
	result = __audit_policy_check(code, ev);
	up_read(&audit_lock);

	return result;
}

/*
 * Attach/detach audit context to process
 */
static int
__audit_attach(struct task_struct *task, int suspended, struct aud_process *parent)
{
	struct aud_process *pinfo, *pnew;
	int		res = 0;

	pnew = audit_alloc();
	task_lock(task);
	if (!(pinfo = task->audit)) {
		task->audit = pinfo = pnew;
		pnew = NULL;
	}
	if (unlikely(!pinfo)) {
		DPRINTF("No memory to attach process %d\n", task->pid);
		res = -ENOMEM;
	}
	else if (unlikely(pinfo->flags & AUD_F_ATTACHED)) {
		DPRINTF("Cannot attach process %d; auditing already enabled\n", task->pid);
		res = -EBUSY;
	}
	else {
		DPRINTF("Attaching process %d\n", task->pid);
		pinfo->flags |= AUD_F_ATTACHED;
		pinfo->suspended = suspended;
	}
	task_unlock(task);
	audit_free(pnew);
	return res;
}

int
audit_attach(int suspended)
{
	/* Don't allow attach if auditd is not there
	 *
	 * XXX: For more robustness, shouldn't we allow the attach to
	 * succeed even if the daemon isn't running? This may happen
	 * if it was restarted due to a crash.
	 */
	if (!audit_message_enabled)
		return -ENODEV;

	return __audit_attach(current, suspended, NULL);
}

static int
__audit_detach(task_t *task, int permanent)
{
	struct aud_process *pinfo;
	int		res = 0;

	task_lock(task);
	if (!(pinfo = task->audit)) {
		task_unlock(task);
		res = -EUNATCH;
	} else {
		/* turn off system call intercept */
		if (permanent)
			task->audit = NULL;
		else {
			pinfo->flags &= ~AUD_F_ATTACHED;
			pinfo->suspended = 1;
		}
		task_unlock(task);

		/* Free any memory we may have allocated for
	   	 * argument data */
		audit_release_arguments(pinfo);
		if (permanent)
			kfree(pinfo);
	}
	return res;
}

int
audit_detach(void)
{
	DPRINTF("detaching process %d\n", current->pid);
	return __audit_detach(current, 1);
}

/*
 * Attach/detach all processes
 */
void
audit_attach_all(void)
{
	task_t	*p;

	read_lock(&tasklist_lock);
	for_each_process(p) {
		if (p != current
		    && p->mm != NULL /*todo this is insufficient to identify kernel threads */
		    && p->pid != 1)
			__audit_attach(p, 0, NULL);
	}
	read_unlock(&tasklist_lock);
}

void
audit_detach_all(void)
{
	task_t	*p;

	read_lock(&tasklist_lock);
	for_each_process(p) {
		__audit_detach(p, 0);
	}
	read_unlock(&tasklist_lock);
}

/*
 * Suspend system call auditing for this process
 */
int
audit_suspend(void)
{
	struct aud_process *pinfo;

	DPRINTF("process %d suspends auditing\n", current->pid);
	if ((pinfo = current->audit) == NULL)
		return -EUNATCH;
	if (!audit_allow_suspend)
		return -EACCES;
	pinfo->suspended++;
	return 0;
}

/*
 * Resume auditing
 */
int
audit_resume(void)
{
	struct aud_process *pinfo;

	DPRINTF("process %d resumes auditing\n", current->pid);
	if ((pinfo = current->audit) == NULL)
		return -EUNATCH;
	pinfo->suspended--;
	return 0;
}

/*
 * Assign an audit ID
 */
int
audit_setauditid(void)
{
	struct aud_process 	*pinfo;

	if (!(pinfo = current->audit))
		return -EUNATCH;

	if (pinfo->audit_id)
		return -EACCES;

	do {
		pinfo->audit_id = ++audit_id;
	} while (!pinfo->audit_id);

	DPRINTF("process %d assigned audit id %u\n",
		       	current->pid, pinfo->audit_id);
	return 0;
}

/*
 * Process login message from user land
 */
int
audit_login(const struct audit_login __user *arg)
{
	struct aud_process	*pinfo;
	struct audit_login	*login;
	struct aud_event_data	ev;
	int			action, err;

	if (!(pinfo = current->audit))
		return -EUNATCH;

	/* Make sure LOGIN works just once */
	if (pinfo->audit_uid != (uid_t) -1)
		return -EACCES;

	if (!(login = kmalloc(sizeof(*login), GFP_KERNEL)))
		return -ENOBUFS;

	err = -EFAULT;
	if (copy_from_user(login, arg, sizeof(*login)))
		goto out;

	err = -EINVAL;
	if (login->uid == (uid_t) -1)
		goto out;

	/* Copy the login uid and keep it */
	pinfo->audit_uid = login->uid;

	/* Notify audit daemon */
	memset(&ev, 0, sizeof(ev));
	strcpy(ev.name, "AUDIT_login");

	action = __audit_policy_check(AUD_POLICY_LOGIN, &ev);
	if (action & AUDIT_LOG)
		err = audit_msg_login(pinfo, ev.name, login);
	else
		err = 0;

out:
	kfree(login);
	return err;
}

/*
 * Pass an audit message generated by user space, and fill in
 * the blanks
 */
int
audit_user_message(const struct audit_message __user *arg)
{
	struct aud_process	*pinfo;
	struct aud_msg_head	*msgh;
	struct audit_message	user_msg;
	struct aud_event_data	ev;
	int			action;

	/* Beware, may be NULL. We still want to allow
	 * un-audited processes to log audit messages. */
	pinfo = current->audit;

	if (copy_from_user(&user_msg, arg, sizeof(user_msg)))
		return -EFAULT;

	if (user_msg.msg_type < AUDIT_MSG_USERBASE)
		return -EACCES;

	memset(&ev, 0, sizeof(ev));
	strncpy(ev.name, user_msg.msg_evname, sizeof(ev.name)-1);

	action = __audit_policy_check(AUD_POLICY_USERMSG, &ev);
	if (!(action & AUDIT_LOG))
		return 0;

	msgh = audit_msg_new(pinfo, user_msg.msg_type,
				user_msg.msg_evname,
				user_msg.msg_size);
	if (IS_ERR(msgh))
		return PTR_ERR(msgh);

	if (copy_from_user(msgh->body.msg_data, user_msg.msg_data, user_msg.msg_size)) {
		audit_msg_release(msgh);
		return -EFAULT;
	}

	audit_msg_insert(msgh);
	return 0;
}

/*
 * Debugging stuff
 */
#ifdef AUDIT_DEBUG_LOCKS
void
_debug_locks(char *ltype, char *var, int lock)
{
	#define NLOCKS 6
	static spinlock_t _debug_lock = SPIN_LOCK_UNLOCKED;
	static int max_cpu = 0;
	static char locks[NR_CPUS][NLOCKS];
	static char out[NR_CPUS * NLOCKS + NR_CPUS];
	int locknum, cpu, p;

	do_spin_lock(&_debug_lock);

	cpu = current->cpu;
	if (cpu > max_cpu) max_cpu = cpu;

	/* get lock state before update */
	for (p=0; p <= max_cpu; ++p) {
		int l;
		for (l=0; l<NLOCKS; ++l) {
			int c;

			c = locks[p][l];
			if (!c) c='.';

			out[p * (NLOCKS+1) + l] = c;
		}
		out[p*(NLOCKS+1)+NLOCKS] = '|';
	}
	out[max_cpu * NLOCKS + max_cpu - 1] = 0;

	if (!strcmp(var, "&audit_lock")) {
		locknum = 0;
	} else if (!strcmp(var, "&audit_message_lock")) {
		locknum = 1;
	} else if (!strcmp(var, "&hook_lock")) {
		locknum = 2;
	} else if (!strcmp(var, "&tasklist_lock")) {
		locknum = 3;
	} else if (!strcmp(var, "task")) {
		locknum = 4;
	} else {
		locknum = 5;
		printk(KERN_DEBUG "unknown lock %s %s %d\n", ltype, var, lock);
	}

	/* mark changed lock w/ capital letter */
	if (lock) {
		int c='l';
		int cp='L';

		if (locks[cpu][locknum]) printk(KERN_DEBUG "double lock?\n");

		if        (!strcmp(ltype, "read")) {
			c='r'; cp='R';
		} else if (!strcmp(ltype, "write")) {
			c='w'; cp='W';
		} else if (!strcmp(ltype, "spin")) {
			c='s'; cp='S';
		} else if (!strcmp(ltype, "task")) {
			c='t'; cp='T';
		}
		
		locks[cpu][locknum] = c;
		out[cpu * (NLOCKS+1) + locknum] = cp;
	} else {
		if (!locks[cpu][locknum]) printk(KERN_DEBUG "double unlock?\n");
		locks[cpu][locknum] = 0;
		out[cpu * (NLOCKS+1) + locknum] = '-';
	}

	printk(KERN_DEBUG "lock state: [%s]\n", out);
	do_spin_unlock(&_debug_lock);
}
#endif

MODULE_DESCRIPTION("Auditing subsystem");
MODULE_LICENSE("GPL");
