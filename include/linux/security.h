/*
 * Linux Security plug
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 * Copyright (C) 2001 James Morris <jmorris@intercode.com.au>
 * Copyright (C) 2001 Silicon Graphics, Inc. (Trust Technology Group)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Due to this file being licensed under the GPL there is controversy over
 *	whether this permits you to write a module that #includes this file
 *	without placing your module under the GPL.  Please consult a lawyer for
 *	advice before doing this.
 *
 */

#ifndef __LINUX_SECURITY_H
#define __LINUX_SECURITY_H

#ifdef __KERNEL__

#include <linux/fs.h>
#include <linux/binfmts.h>
#include <linux/signal.h>
#include <linux/resource.h>
#include <linux/sem.h>
#include <linux/sysctl.h>
#include <linux/shm.h>
#include <linux/msg.h>

/*
 * Values used in the task_security_ops calls
 */
/* setuid or setgid, id0 == uid or gid */
#define LSM_SETID_ID	1

/* setreuid or setregid, id0 == real, id1 == eff */
#define LSM_SETID_RE	2

/* setresuid or setresgid, id0 == real, id1 == eff, uid2 == saved */
#define LSM_SETID_RES	4

/* setfsuid or setfsgid, id0 == fsuid or fsgid */
#define LSM_SETID_FS	8

/* forward declares to avoid warnings */
struct sk_buff;
struct net_device;
struct nfsctl_arg;
struct sched_param;
struct swap_info_struct;

/**
 * struct security_operations - main security structure
 *
 * Security hooks for program execution operations.
 *
 * @bprm_alloc_security:
 *	Allocate and attach a security structure to the @bprm->security field.
 *	The security field is initialized to NULL when the bprm structure is
 *	allocated.
 *	@bprm contains the linux_binprm structure to be modified.
 *	Return 0 if operation was successful.
 * @bprm_free_security:
 *	@bprm contains the linux_binprm structure to be modified.
 *	Deallocate and clear the @bprm->security field.
 * @bprm_compute_creds:
 *	Compute and set the security attributes of a process being transformed
 *	by an execve operation based on the old attributes (current->security)
 *	and the information saved in @bprm->security by the set_security hook.
 *	Since this hook function (and its caller) are void, this hook can not
 *	return an error.  However, it can leave the security attributes of the
 *	process unchanged if an access failure occurs at this point. It can
 *	also perform other state changes on the process (e.g.  closing open
 *	file descriptors to which access is no longer granted if the attributes
 *	were changed). 
 *	@bprm contains the linux_binprm structure.
 * @bprm_set_security:
 *	Save security information in the bprm->security field, typically based
 *	on information about the bprm->file, for later use by the compute_creds
 *	hook.  This hook may also optionally check permissions (e.g. for
 *	transitions between security domains).
 *	This hook may be called multiple times during a single execve, e.g. for
 *	interpreters.  The hook can tell whether it has already been called by
 *	checking to see if @bprm->security is non-NULL.  If so, then the hook
 *	may decide either to retain the security information saved earlier or
 *	to replace it.
 *	@bprm contains the linux_binprm structure.
 *	Return 0 if the hook is successful and permission is granted.
 * @bprm_check_security:
 * 	This hook mediates the point when a search for a binary handler	will
 * 	begin.  It allows a check the @bprm->security value which is set in
 * 	the preceding set_security call.  The primary difference from
 * 	set_security is that the argv list and envp list are reliably
 * 	available in @bprm.  This hook may be called multiple times
 * 	during a single execve; and in each pass set_security is called
 * 	first.
 * 	@bprm contains the linux_binprm structure.
 *	Return 0 if the hook is successful and permission is granted.
 *
 * Security hooks for task operations.
 *
 * @task_create:
 *	Check permission before creating a child process.  See the clone(2)
 *	manual page for definitions of the @clone_flags.
 *	@clone_flags contains the flags indicating what should be shared.
 *	Return 0 if permission is granted.
 * @task_alloc_security:
 *	@p contains the task_struct for child process.
 *	Allocate and attach a security structure to the p->security field. The
 *	security field is initialized to NULL when the task structure is
 *	allocated.
 *	Return 0 if operation was successful.
 * @task_free_security:
 *	@p contains the task_struct for process.
 *	Deallocate and clear the p->security field.
 * @task_setuid:
 *	Check permission before setting one or more of the user identity
 *	attributes of the current process.  The @flags parameter indicates
 *	which of the set*uid system calls invoked this hook and how to
 *	interpret the @id0, @id1, and @id2 parameters.  See the LSM_SETID
 *	definitions at the beginning of this file for the @flags values and
 *	their meanings.
 *	@id0 contains a uid.
 *	@id1 contains a uid.
 *	@id2 contains a uid.
 *	@flags contains one of the LSM_SETID_* values.
 *	Return 0 if permission is granted.
 * @task_post_setuid:
 *	Update the module's state after setting one or more of the user
 *	identity attributes of the current process.  The @flags parameter
 *	indicates which of the set*uid system calls invoked this hook.  If
 *	@flags is LSM_SETID_FS, then @old_ruid is the old fs uid and the other
 *	parameters are not used.
 *	@old_ruid contains the old real uid (or fs uid if LSM_SETID_FS).
 *	@old_euid contains the old effective uid (or -1 if LSM_SETID_FS).
 *	@old_suid contains the old saved uid (or -1 if LSM_SETID_FS).
 *	@flags contains one of the LSM_SETID_* values.
 *	Return 0 on success.
 * @task_setgid:
 *	Check permission before setting one or more of the group identity
 *	attributes of the current process.  The @flags parameter indicates
 *	which of the set*gid system calls invoked this hook and how to
 *	interpret the @id0, @id1, and @id2 parameters.  See the LSM_SETID
 *	definitions at the beginning of this file for the @flags values and
 *	their meanings.
 *	@id0 contains a gid.
 *	@id1 contains a gid.
 *	@id2 contains a gid.
 *	@flags contains one of the LSM_SETID_* values.
 *	Return 0 if permission is granted.
 * @task_setpgid:
 *	Check permission before setting the process group identifier of the
 *	process @p to @pgid.
 *	@p contains the task_struct for process being modified.
 *	@pgid contains the new pgid.
 *	Return 0 if permission is granted.
 * @task_getpgid:
 *	Check permission before getting the process group identifier of the
 *	process @p.
 *	@p contains the task_struct for the process.
 *	Return 0 if permission is granted.
 * @task_getsid:
 *	Check permission before getting the session identifier of the process
 *	@p.
 *	@p contains the task_struct for the process.
 *	Return 0 if permission is granted.
 * @task_setgroups:
 *	Check permission before setting the supplementary group set of the
 *	current process to @grouplist.
 *	@gidsetsize contains the number of elements in @grouplist.
 *	@grouplist contains the array of gids.
 *	Return 0 if permission is granted.
 * @task_setnice:
 *	Check permission before setting the nice value of @p to @nice.
 *	@p contains the task_struct of process.
 *	@nice contains the new nice value.
 *	Return 0 if permission is granted.
 * @task_setrlimit:
 *	Check permission before setting the resource limits of the current
 *	process for @resource to @new_rlim.  The old resource limit values can
 *	be examined by dereferencing (current->rlim + resource).
 *	@resource contains the resource whose limit is being set.
 *	@new_rlim contains the new limits for @resource.
 *	Return 0 if permission is granted.
 * @task_setscheduler:
 *	Check permission before setting scheduling policy and/or parameters of
 *	process @p based on @policy and @lp.
 *	@p contains the task_struct for process.
 *	@policy contains the scheduling policy.
 *	@lp contains the scheduling parameters.
 *	Return 0 if permission is granted.
 * @task_getscheduler:
 *	Check permission before obtaining scheduling information for process
 *	@p.
 *	@p contains the task_struct for process.
 *	Return 0 if permission is granted.
 * @task_kill:
 *	Check permission before sending signal @sig to @p.  @info can be NULL,
 *	the constant 1, or a pointer to a siginfo structure.  If @info is 1 or
 *	SI_FROMKERNEL(info) is true, then the signal should be viewed as coming
 *	from the kernel and should typically be permitted.
 *	SIGIO signals are handled separately by the send_sigiotask hook in
 *	file_security_ops.
 *	@p contains the task_struct for process.
 *	@info contains the signal information.
 *	@sig contains the signal value.
 *	Return 0 if permission is granted.
 * @task_wait:
 *	Check permission before allowing a process to reap a child process @p
 *	and collect its status information.
 *	@p contains the task_struct for process.
 *	Return 0 if permission is granted.
 * @task_prctl:
 *	Check permission before performing a process control operation on the
 *	current process.
 *	@option contains the operation.
 *	@arg2 contains a argument.
 *	@arg3 contains a argument.
 *	@arg4 contains a argument.
 *	@arg5 contains a argument.
 *	Return 0 if permission is granted.
 * @task_kmod_set_label:
 *	Set the security attributes in current->security for the kernel module
 *	loader thread, so that it has the permissions needed to perform its
 *	function.
 * @task_reparent_to_init:
 * 	Set the security attributes in @p->security for a kernel thread that
 * 	is being reparented to the init task.
 *	@p contains the task_struct for the kernel thread.
 *
 * @ptrace:
 *	Check permission before allowing the @parent process to trace the
 *	@child process.
 *	Security modules may also want to perform a process tracing check
 *	during an execve in the set_security or compute_creds hooks of
 *	binprm_security_ops if the process is being traced and its security
 *	attributes would be changed by the execve.
 *	@parent contains the task_struct structure for parent process.
 *	@child contains the task_struct structure for child process.
 *	Return 0 if permission is granted.
 * @capget:
 *	Get the @effective, @inheritable, and @permitted capability sets for
 *	the @target process.  The hook may also perform permission checking to
 *	determine if the current process is allowed to see the capability sets
 *	of the @target process.
 *	@target contains the task_struct structure for target process.
 *	@effective contains the effective capability set.
 *	@inheritable contains the inheritable capability set.
 *	@permitted contains the permitted capability set.
 *	Return 0 if the capability sets were successfully obtained.
 * @capset_check:
 *	Check permission before setting the @effective, @inheritable, and
 *	@permitted capability sets for the @target process.
 *	Caveat:  @target is also set to current if a set of processes is
 *	specified (i.e. all processes other than current and init or a
 *	particular process group).  Hence, the capset_set hook may need to
 *	revalidate permission to the actual target process.
 *	@target contains the task_struct structure for target process.
 *	@effective contains the effective capability set.
 *	@inheritable contains the inheritable capability set.
 *	@permitted contains the permitted capability set.
 *	Return 0 if permission is granted.
 * @capset_set:
 *	Set the @effective, @inheritable, and @permitted capability sets for
 *	the @target process.  Since capset_check cannot always check permission
 *	to the real @target process, this hook may also perform permission
 *	checking to determine if the current process is allowed to set the
 *	capability sets of the @target process.  However, this hook has no way
 *	of returning an error due to the structure of the sys_capset code.
 *	@target contains the task_struct structure for target process.
 *	@effective contains the effective capability set.
 *	@inheritable contains the inheritable capability set.
 *	@permitted contains the permitted capability set.
 * @capable:
 *	Check whether the @tsk process has the @cap capability.
 *	@tsk contains the task_struct for the process.
 *	@cap contains the capability <include/linux/capability.h>.
 *	Return 0 if the capability is granted for @tsk.
 * @sys_security:
 *	Security modules may use this hook to implement new system calls for
 *	security-aware applications.  The interface is similar to socketcall,
 *	but with an @id parameter to help identify the security module whose
 *	call is being invoked.  The module is responsible for interpreting the
 *	parameters, and must copy in the @args array from user space if it is
 *	used.
 *	The recommended convention for creating the hexadecimal @id value is
 *	echo "Name_of_module" | md5sum | cut -c -8; by using this convention,
 *	there is no need for a central registry.
 *	@id contains the security module identifier.
 *	@call contains the call value.
 *	@args contains the call arguments (user space pointer).
 *	The module should return -ENOSYS if it does not implement any new
 *	system calls.
 *
 * @register_security:
 * 	allow module stacking.
 * 	@name contains the name of the security module being stacked.
 * 	@ops contains a pointer to the struct security_operations of the module to stack.
 * @unregister_security:
 *	remove a stacked module.
 *	@name contains the name of the security module being unstacked.
 *	@ops contains a pointer to the struct security_operations of the module to unstack.
 * 
 * This is the main security structure.
 */
struct security_operations {
	int (*ptrace) (struct task_struct * parent, struct task_struct * child);
	int (*capget) (struct task_struct * target,
		       kernel_cap_t * effective,
		       kernel_cap_t * inheritable, kernel_cap_t * permitted);
	int (*capset_check) (struct task_struct * target,
			     kernel_cap_t * effective,
			     kernel_cap_t * inheritable,
			     kernel_cap_t * permitted);
	void (*capset_set) (struct task_struct * target,
			    kernel_cap_t * effective,
			    kernel_cap_t * inheritable,
			    kernel_cap_t * permitted);
	int (*capable) (struct task_struct * tsk, int cap);
	int (*sys_security) (unsigned int id, unsigned call,
			     unsigned long *args);

	int (*bprm_alloc_security) (struct linux_binprm * bprm);
	void (*bprm_free_security) (struct linux_binprm * bprm);
	void (*bprm_compute_creds) (struct linux_binprm * bprm);
	int (*bprm_set_security) (struct linux_binprm * bprm);
	int (*bprm_check_security) (struct linux_binprm * bprm);

	int (*task_create) (unsigned long clone_flags);
	int (*task_alloc_security) (struct task_struct * p);
	void (*task_free_security) (struct task_struct * p);
	int (*task_setuid) (uid_t id0, uid_t id1, uid_t id2, int flags);
	int (*task_post_setuid) (uid_t old_ruid /* or fsuid */ ,
				 uid_t old_euid, uid_t old_suid, int flags);
	int (*task_setgid) (gid_t id0, gid_t id1, gid_t id2, int flags);
	int (*task_setpgid) (struct task_struct * p, pid_t pgid);
	int (*task_getpgid) (struct task_struct * p);
	int (*task_getsid) (struct task_struct * p);
	int (*task_setgroups) (int gidsetsize, gid_t * grouplist);
	int (*task_setnice) (struct task_struct * p, int nice);
	int (*task_setrlimit) (unsigned int resource, struct rlimit * new_rlim);
	int (*task_setscheduler) (struct task_struct * p, int policy,
				  struct sched_param * lp);
	int (*task_getscheduler) (struct task_struct * p);
	int (*task_kill) (struct task_struct * p,
			  struct siginfo * info, int sig);
	int (*task_wait) (struct task_struct * p);
	int (*task_prctl) (int option, unsigned long arg2,
			   unsigned long arg3, unsigned long arg4,
			   unsigned long arg5);
	void (*task_kmod_set_label) (void);
	void (*task_reparent_to_init) (struct task_struct * p);

	/* allow module stacking */
	int (*register_security) (const char *name,
	                          struct security_operations *ops);
	int (*unregister_security) (const char *name,
	                            struct security_operations *ops);
};


/* prototypes */
extern int security_scaffolding_startup	(void);
extern int register_security	(struct security_operations *ops);
extern int unregister_security	(struct security_operations *ops);
extern int mod_reg_security	(const char *name, struct security_operations *ops);
extern int mod_unreg_security	(const char *name, struct security_operations *ops);
extern int capable		(int cap);

/* global variables */
extern struct security_operations *security_ops;


#endif /* __KERNEL__ */

#endif /* ! __LINUX_SECURITY_H */

