/*
 * Audit subsystem module hooks
 *
 * Copyright (C) 2003, SuSE Linux AG
 * Written by okir@suse.de
 */

#define __NO_VERSION__
#define DONT_DEBUG_LOCKS
#include "audit-private.h"

struct aud_process *audit_alloc(void)
{
	struct aud_process *pinfo = kmalloc(sizeof(*pinfo), GFP_KERNEL);

	if (pinfo) {
		memset(pinfo, 0, sizeof(*pinfo));
		pinfo->audit_uid = (uid_t) -1;
		pinfo->suspended = 1;
	}
	return pinfo;
}

#ifdef CONFIG_AUDIT_MODULE

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/sys.h>
#include <asm/semaphore.h>

EXPORT_SYMBOL(audit_alloc);

static struct audit_hooks audit;
static DECLARE_RWSEM(hook_lock);

int
audit_register(const struct audit_hooks *hooks)
{
	int	res = 0;

	if (!hooks->intercept
	 || !hooks->result
	 || !hooks->fork
	 || !hooks->exit)
		return -EINVAL;

	down_write(&hook_lock);
	if (audit.intercept) {
		res = -EEXIST;
	} else {
		audit = *hooks;
	}
	mb();
	up_write(&hook_lock);

	return res;
}

EXPORT_SYMBOL(audit_register);

void
audit_unregister(void)
{
	down_write(&hook_lock);
	memset(&audit, 0, sizeof(audit));
	mb();
	up_write(&hook_lock);
}

EXPORT_SYMBOL(audit_unregister);

void
audit_intercept(enum audit_call code, ...)
{
	int res = 0;

	down_read(&hook_lock);
	if (audit.intercept) {
		va_list varg;

		va_start(varg, code);
		res = audit.intercept(code, varg);
	}
	up_read(&hook_lock);
	if (res < 0)
		audit_kill_process(res);
}

long
audit_lresult(long result)
{
	down_read(&hook_lock);
	if (audit.result)
		result = audit.result(result);
	up_read(&hook_lock);
	return result;
}

void
audit_fork(struct task_struct *parent, struct task_struct *child)
{
	down_read(&hook_lock);
	if (audit.fork)
		audit.fork(parent, child);
	up_read(&hook_lock);
}

void
audit_exit(struct task_struct *task, long code)
{
	down_read(&hook_lock);
	if (audit.exit)
		audit.exit(task, code);
	up_read(&hook_lock);
}

void
audit_netlink_msg(struct sk_buff *skb, int res)
{
	down_read(&hook_lock);
	if (audit.netlink_msg)
		audit.netlink_msg(skb, res);
	up_read(&hook_lock);
}

#endif /* CONFIG_AUDIT_MODULE */
