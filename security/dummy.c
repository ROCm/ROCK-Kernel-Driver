/*
 * Stub functions for the default security function pointers in case no
 * security model is loaded.
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>

static int dummy_ptrace (struct task_struct *parent, struct task_struct *child)
{
	return 0;
}

static int dummy_capget (struct task_struct *target, kernel_cap_t * effective,
			 kernel_cap_t * inheritable, kernel_cap_t * permitted)
{
	return 0;
}

static int dummy_capset_check (struct task_struct *target,
			       kernel_cap_t * effective,
			       kernel_cap_t * inheritable,
			       kernel_cap_t * permitted)
{
	return 0;
}

static void dummy_capset_set (struct task_struct *target,
			      kernel_cap_t * effective,
			      kernel_cap_t * inheritable,
			      kernel_cap_t * permitted)
{
	return;
}

static int dummy_capable (struct task_struct *tsk, int cap)
{
	if (cap_is_fs_cap (cap) ? tsk->fsuid == 0 : tsk->euid == 0)
		/* capability granted */
		return 0;

	/* capability denied */
	return -EPERM;
}

static int dummy_sys_security (unsigned int id, unsigned int call,
			       unsigned long *args)
{
	return -ENOSYS;
}

static int dummy_bprm_alloc_security (struct linux_binprm *bprm)
{
	return 0;
}

static void dummy_bprm_free_security (struct linux_binprm *bprm)
{
	return;
}

static void dummy_bprm_compute_creds (struct linux_binprm *bprm)
{
	return;
}

static int dummy_bprm_set_security (struct linux_binprm *bprm)
{
	return 0;
}

static int dummy_bprm_check_security (struct linux_binprm *bprm)
{
	return 0;
}

static int dummy_task_create (unsigned long clone_flags)
{
	return 0;
}

static int dummy_task_alloc_security (struct task_struct *p)
{
	return 0;
}

static void dummy_task_free_security (struct task_struct *p)
{
	return;
}

static int dummy_task_setuid (uid_t id0, uid_t id1, uid_t id2, int flags)
{
	return 0;
}

static int dummy_task_post_setuid (uid_t id0, uid_t id1, uid_t id2, int flags)
{
	return 0;
}

static int dummy_task_setgid (gid_t id0, gid_t id1, gid_t id2, int flags)
{
	return 0;
}

static int dummy_task_setpgid (struct task_struct *p, pid_t pgid)
{
	return 0;
}

static int dummy_task_getpgid (struct task_struct *p)
{
	return 0;
}

static int dummy_task_getsid (struct task_struct *p)
{
	return 0;
}

static int dummy_task_setgroups (int gidsetsize, gid_t * grouplist)
{
	return 0;
}

static int dummy_task_setnice (struct task_struct *p, int nice)
{
	return 0;
}

static int dummy_task_setrlimit (unsigned int resource, struct rlimit *new_rlim)
{
	return 0;
}

static int dummy_task_setscheduler (struct task_struct *p, int policy,
				    struct sched_param *lp)
{
	return 0;
}

static int dummy_task_getscheduler (struct task_struct *p)
{
	return 0;
}

static int dummy_task_wait (struct task_struct *p)
{
	return 0;
}

static int dummy_task_kill (struct task_struct *p, struct siginfo *info,
			    int sig)
{
	return 0;
}

static int dummy_task_prctl (int option, unsigned long arg2, unsigned long arg3,
			     unsigned long arg4, unsigned long arg5)
{
	return 0;
}

static void dummy_task_kmod_set_label (void)
{
	return;
}

static void dummy_task_reparent_to_init (struct task_struct *p)
{
	p->euid = p->fsuid = 0;
	return;
}

static int dummy_register (const char *name, struct security_operations *ops)
{
	return -EINVAL;
}

static int dummy_unregister (const char *name, struct security_operations *ops)
{
	return -EINVAL;
}

struct security_operations dummy_security_ops = {
	ptrace:				dummy_ptrace,
	capget:				dummy_capget,
	capset_check:			dummy_capset_check,
	capset_set:			dummy_capset_set,
	capable:			dummy_capable,
	sys_security:			dummy_sys_security,
	
	bprm_alloc_security:		dummy_bprm_alloc_security,
	bprm_free_security:		dummy_bprm_free_security,
	bprm_compute_creds:		dummy_bprm_compute_creds,
	bprm_set_security:		dummy_bprm_set_security,
	bprm_check_security:		dummy_bprm_check_security,

	task_create:			dummy_task_create,
	task_alloc_security:		dummy_task_alloc_security,
	task_free_security:		dummy_task_free_security,
	task_setuid:			dummy_task_setuid,
	task_post_setuid:		dummy_task_post_setuid,
	task_setgid:			dummy_task_setgid,
	task_setpgid:			dummy_task_setpgid,
	task_getpgid:			dummy_task_getpgid,
	task_getsid:			dummy_task_getsid,
	task_setgroups:			dummy_task_setgroups,
	task_setnice:			dummy_task_setnice,
	task_setrlimit:			dummy_task_setrlimit,
	task_setscheduler:		dummy_task_setscheduler,
	task_getscheduler:		dummy_task_getscheduler,
	task_wait:			dummy_task_wait,
	task_kill:			dummy_task_kill,
	task_prctl:			dummy_task_prctl,
	task_kmod_set_label:		dummy_task_kmod_set_label,
	task_reparent_to_init:		dummy_task_reparent_to_init,
	
	register_security:		dummy_register,
	unregister_security:		dummy_unregister,
};

