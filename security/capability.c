/*
 *  Capabilities Linux Security Module
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>

/* flag to keep track of how we were registered */
static int secondary;

static int cap_capable (struct task_struct *tsk, int cap)
{
	/* Derived from include/linux/sched.h:capable. */
	if (cap_raised (tsk->cap_effective, cap))
		return 0;
	else
		return -EPERM;
}

static int cap_sys_security (unsigned int id, unsigned int call,
			     unsigned long *args)
{
	return -ENOSYS;
}

static int cap_ptrace (struct task_struct *parent, struct task_struct *child)
{
	/* Derived from arch/i386/kernel/ptrace.c:sys_ptrace. */
	if (!cap_issubset (child->cap_permitted, current->cap_permitted) &&
	    !capable (CAP_SYS_PTRACE))
		return -EPERM;
	else
		return 0;
}

static int cap_capget (struct task_struct *target, kernel_cap_t * effective,
		       kernel_cap_t * inheritable, kernel_cap_t * permitted)
{
	/* Derived from kernel/capability.c:sys_capget. */
	*effective = cap_t (target->cap_effective);
	*inheritable = cap_t (target->cap_inheritable);
	*permitted = cap_t (target->cap_permitted);
	return 0;
}

static int cap_capset_check (struct task_struct *target,
			     kernel_cap_t * effective,
			     kernel_cap_t * inheritable,
			     kernel_cap_t * permitted)
{
	/* Derived from kernel/capability.c:sys_capset. */
	/* verify restrictions on target's new Inheritable set */
	if (!cap_issubset (*inheritable,
			   cap_combine (target->cap_inheritable,
					current->cap_permitted))) {
		return -EPERM;
	}

	/* verify restrictions on target's new Permitted set */
	if (!cap_issubset (*permitted,
			   cap_combine (target->cap_permitted,
					current->cap_permitted))) {
		return -EPERM;
	}

	/* verify the _new_Effective_ is a subset of the _new_Permitted_ */
	if (!cap_issubset (*effective, *permitted)) {
		return -EPERM;
	}

	return 0;
}

static void cap_capset_set (struct task_struct *target,
			    kernel_cap_t * effective,
			    kernel_cap_t * inheritable,
			    kernel_cap_t * permitted)
{
	target->cap_effective = *effective;
	target->cap_inheritable = *inheritable;
	target->cap_permitted = *permitted;
}

static int cap_bprm_alloc_security (struct linux_binprm *bprm)
{
	return 0;
}

static int cap_bprm_set_security (struct linux_binprm *bprm)
{
	/* Copied from fs/exec.c:prepare_binprm. */

	/* We don't have VFS support for capabilities yet */
	cap_clear (bprm->cap_inheritable);
	cap_clear (bprm->cap_permitted);
	cap_clear (bprm->cap_effective);

	/*  To support inheritance of root-permissions and suid-root
	 *  executables under compatibility mode, we raise all three
	 *  capability sets for the file.
	 *
	 *  If only the real uid is 0, we only raise the inheritable
	 *  and permitted sets of the executable file.
	 */

	if (!issecure (SECURE_NOROOT)) {
		if (bprm->e_uid == 0 || current->uid == 0) {
			cap_set_full (bprm->cap_inheritable);
			cap_set_full (bprm->cap_permitted);
		}
		if (bprm->e_uid == 0)
			cap_set_full (bprm->cap_effective);
	}
	return 0;
}

static int cap_bprm_check_security (struct linux_binprm *bprm)
{
	return 0;
}

static void cap_bprm_free_security (struct linux_binprm *bprm)
{
	return;
}

/* Copied from fs/exec.c */
static inline int must_not_trace_exec (struct task_struct *p)
{
	return (p->ptrace & PT_PTRACED) && !(p->ptrace & PT_PTRACE_CAP);
}

static void cap_bprm_compute_creds (struct linux_binprm *bprm)
{
	/* Derived from fs/exec.c:compute_creds. */
	kernel_cap_t new_permitted, working;
	int do_unlock = 0;

	new_permitted = cap_intersect (bprm->cap_permitted, cap_bset);
	working = cap_intersect (bprm->cap_inheritable,
				 current->cap_inheritable);
	new_permitted = cap_combine (new_permitted, working);

	if (!cap_issubset (new_permitted, current->cap_permitted)) {
		current->mm->dumpable = 0;

		lock_kernel ();
		if (must_not_trace_exec (current)
		    || atomic_read (&current->fs->count) > 1
		    || atomic_read (&current->files->count) > 1
		    || atomic_read (&current->sig->count) > 1) {
			if (!capable (CAP_SETPCAP)) {
				new_permitted = cap_intersect (new_permitted,
							       current->
							       cap_permitted);
			}
		}
		do_unlock = 1;
	}

	/* For init, we want to retain the capabilities set
	 * in the init_task struct. Thus we skip the usual
	 * capability rules */
	if (current->pid != 1) {
		current->cap_permitted = new_permitted;
		current->cap_effective =
		    cap_intersect (new_permitted, bprm->cap_effective);
	}

	/* AUD: Audit candidate if current->cap_effective is set */

	if (do_unlock)
		unlock_kernel ();

	current->keep_capabilities = 0;
}

static int cap_task_create (unsigned long clone_flags)
{
	return 0;
}

static int cap_task_alloc_security (struct task_struct *p)
{
	return 0;
}

static void cap_task_free_security (struct task_struct *p)
{
	return;
}

static int cap_task_setuid (uid_t id0, uid_t id1, uid_t id2, int flags)
{
	return 0;
}

/* moved from kernel/sys.c. */
/* 
 * cap_emulate_setxuid() fixes the effective / permitted capabilities of
 * a process after a call to setuid, setreuid, or setresuid.
 *
 *  1) When set*uiding _from_ one of {r,e,s}uid == 0 _to_ all of
 *  {r,e,s}uid != 0, the permitted and effective capabilities are
 *  cleared.
 *
 *  2) When set*uiding _from_ euid == 0 _to_ euid != 0, the effective
 *  capabilities of the process are cleared.
 *
 *  3) When set*uiding _from_ euid != 0 _to_ euid == 0, the effective
 *  capabilities are set to the permitted capabilities.
 *
 *  fsuid is handled elsewhere. fsuid == 0 and {r,e,s}uid!= 0 should 
 *  never happen.
 *
 *  -astor 
 *
 * cevans - New behaviour, Oct '99
 * A process may, via prctl(), elect to keep its capabilities when it
 * calls setuid() and switches away from uid==0. Both permitted and
 * effective sets will be retained.
 * Without this change, it was impossible for a daemon to drop only some
 * of its privilege. The call to setuid(!=0) would drop all privileges!
 * Keeping uid 0 is not an option because uid 0 owns too many vital
 * files..
 * Thanks to Olaf Kirch and Peter Benie for spotting this.
 */
static inline void cap_emulate_setxuid (int old_ruid, int old_euid,
					int old_suid)
{
	if ((old_ruid == 0 || old_euid == 0 || old_suid == 0) &&
	    (current->uid != 0 && current->euid != 0 && current->suid != 0) &&
	    !current->keep_capabilities) {
		cap_clear (current->cap_permitted);
		cap_clear (current->cap_effective);
	}
	if (old_euid == 0 && current->euid != 0) {
		cap_clear (current->cap_effective);
	}
	if (old_euid != 0 && current->euid == 0) {
		current->cap_effective = current->cap_permitted;
	}
}

static int cap_task_post_setuid (uid_t old_ruid, uid_t old_euid, uid_t old_suid,
				 int flags)
{
	switch (flags) {
	case LSM_SETID_RE:
	case LSM_SETID_ID:
	case LSM_SETID_RES:
		/* Copied from kernel/sys.c:setreuid/setuid/setresuid. */
		if (!issecure (SECURE_NO_SETUID_FIXUP)) {
			cap_emulate_setxuid (old_ruid, old_euid, old_suid);
		}
		break;
	case LSM_SETID_FS:
		{
			uid_t old_fsuid = old_ruid;

			/* Copied from kernel/sys.c:setfsuid. */

			/*
			 * FIXME - is fsuser used for all CAP_FS_MASK capabilities?
			 *          if not, we might be a bit too harsh here.
			 */

			if (!issecure (SECURE_NO_SETUID_FIXUP)) {
				if (old_fsuid == 0 && current->fsuid != 0) {
					cap_t (current->cap_effective) &=
					    ~CAP_FS_MASK;
				}
				if (old_fsuid != 0 && current->fsuid == 0) {
					cap_t (current->cap_effective) |=
					    (cap_t (current->cap_permitted) &
					     CAP_FS_MASK);
				}
			}
			break;
		}
	default:
		return -EINVAL;
	}

	return 0;
}

static int cap_task_setgid (gid_t id0, gid_t id1, gid_t id2, int flags)
{
	return 0;
}

static int cap_task_setpgid (struct task_struct *p, pid_t pgid)
{
	return 0;
}

static int cap_task_getpgid (struct task_struct *p)
{
	return 0;
}

static int cap_task_getsid (struct task_struct *p)
{
	return 0;
}

static int cap_task_setgroups (int gidsetsize, gid_t * grouplist)
{
	return 0;
}

static int cap_task_setnice (struct task_struct *p, int nice)
{
	return 0;
}

static int cap_task_setrlimit (unsigned int resource, struct rlimit *new_rlim)
{
	return 0;
}

static int cap_task_setscheduler (struct task_struct *p, int policy,
				  struct sched_param *lp)
{
	return 0;
}

static int cap_task_getscheduler (struct task_struct *p)
{
	return 0;
}

static int cap_task_wait (struct task_struct *p)
{
	return 0;
}

static int cap_task_kill (struct task_struct *p, struct siginfo *info, int sig)
{
	return 0;
}

static int cap_task_prctl (int option, unsigned long arg2, unsigned long arg3,
			   unsigned long arg4, unsigned long arg5)
{
	return 0;
}

static void cap_task_kmod_set_label (void)
{
	cap_set_full (current->cap_effective);
	return;
}

static void cap_task_reparent_to_init (struct task_struct *p)
{
	p->cap_effective = CAP_INIT_EFF_SET;
	p->cap_inheritable = CAP_INIT_INH_SET;
	p->cap_permitted = CAP_FULL_SET;
	p->keep_capabilities = 0;
	return;
}

static int cap_register (const char *name, struct security_operations *ops)
{
	return -EINVAL;
}

static int cap_unregister (const char *name, struct security_operations *ops)
{
	return -EINVAL;
}

static struct security_operations capability_ops = {
	ptrace:				cap_ptrace,
	capget:				cap_capget,
	capset_check:			cap_capset_check,
	capset_set:			cap_capset_set,
	capable:			cap_capable,
	sys_security:			cap_sys_security,
	
	bprm_alloc_security:		cap_bprm_alloc_security,
	bprm_free_security:		cap_bprm_free_security,
	bprm_compute_creds:		cap_bprm_compute_creds,
	bprm_set_security:		cap_bprm_set_security,
	bprm_check_security:		cap_bprm_check_security,
	
	task_create:			cap_task_create,
	task_alloc_security:		cap_task_alloc_security,
	task_free_security:		cap_task_free_security,
	task_setuid:			cap_task_setuid,
	task_post_setuid:		cap_task_post_setuid,
	task_setgid:			cap_task_setgid,
	task_setpgid:			cap_task_setpgid,
	task_getpgid:			cap_task_getpgid,
	task_getsid:			cap_task_getsid,
	task_setgroups:			cap_task_setgroups,
	task_setnice:			cap_task_setnice,
	task_setrlimit:			cap_task_setrlimit,
	task_setscheduler:		cap_task_setscheduler,
	task_getscheduler:		cap_task_getscheduler,
	task_wait:			cap_task_wait,
	task_kill:			cap_task_kill,
	task_prctl:			cap_task_prctl,
	task_kmod_set_label:		cap_task_kmod_set_label,
	task_reparent_to_init:		cap_task_reparent_to_init,
	
	register_security:		cap_register,
	unregister_security:		cap_unregister,
};

#if defined(CONFIG_SECURITY_CAPABILITIES_MODULE)
#define MY_NAME THIS_MODULE->name
#else
#define MY_NAME "capability"
#endif

static int __init capability_init (void)
{
	/* register ourselves with the security framework */
	if (register_security (&capability_ops)) {
		printk (KERN_INFO
			"Failure registering capabilities with the kernel\n");
		/* try registering with primary module */
		if (mod_reg_security (MY_NAME, &capability_ops)) {
			printk (KERN_INFO "Failure registering capabilities "
				"with primary security module.\n");
			return -EINVAL;
		}
		secondary = 1;
	}
	printk (KERN_INFO "Capability LSM initialized\n");
	return 0;
}

static void __exit capability_exit (void)
{
	/* remove ourselves from the security framework */
	if (secondary) {
		if (mod_unreg_security (MY_NAME, &capability_ops))
			printk (KERN_INFO "Failure unregistering capabilities "
				"with primary module.\n");
		return;
	}

	if (unregister_security (&capability_ops)) {
		printk (KERN_INFO
			"Failure unregistering capabilities with the kernel\n");
	}
}

module_init (capability_init);
module_exit (capability_exit);

MODULE_DESCRIPTION("Standard Linux Capabilities Security Module");
MODULE_LICENSE("GPL");
