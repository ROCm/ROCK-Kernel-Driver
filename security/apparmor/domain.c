/*
 * AppArmor security module
 *
 * This file contains AppArmor policy attachment and domain transitions
 *
 * Copyright (C) 2002-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/errno.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/syscalls.h>
#include <linux/tracehook.h>
#include <linux/personality.h>

#include "include/audit.h"
#include "include/security/apparmorfs.h"
#include "include/context.h"
#include "include/domain.h"
#include "include/file.h"
#include "include/ipc.h"
#include "include/match.h"
#include "include/path.h"
#include "include/policy.h"

/**
 * aa_free_domain_entries - free entries in a domain table
 * @domain: the domain table to free
 */
void aa_free_domain_entries(struct aa_domain *domain)
{
	int i;

	if (!domain->table)
		return;

	for (i = 0; i < domain->size; i++)
		kfree(domain->table[i]);
	kfree(domain->table);
}

/*
 * check if the task is ptraced and if so if the tracing task is allowed
 * to trace the new domain
 */
static int aa_may_change_ptraced_domain(struct task_struct *task,
					struct aa_profile *to_profile)
{
	struct task_struct *tracer;
	struct cred *cred = NULL;
	struct aa_profile *tracerp = NULL;
	int error = 0;

	rcu_read_lock();
	tracer = tracehook_tracer_task(task);
	if (tracer)
		cred = aa_get_task_policy(tracer, &tracerp);
	rcu_read_unlock();

	if (!tracerp)
		return error;

	error = aa_may_ptrace(tracer, tracerp, to_profile, PTRACE_MODE_ATTACH);
	put_cred(cred);

	return error;
}

/**
 * change_profile_perms
 */
static struct file_perms change_profile_perms(struct aa_profile *profile,
					      struct aa_namespace *ns,
					      const char *name,
					      unsigned int *rstate)
{
	struct file_perms perms;
	struct path_cond cond = { 0, 0 };
	unsigned int state;

	if (!profile) {
		/* unconfined */
		perms.allowed = AA_MAY_CHANGE_PROFILE;
		perms.xindex = perms.dindex = 0;
		perms.audit = perms.quiet = perms.kill = 0;
		if (rstate)
			*rstate = 0;
		return perms;
	} else if (!profile->file.dfa) {
		return nullperms;
	} else if ((ns == profile->ns)) {
		/* try matching against rules with out namespace prependend */
		perms = aa_str_perms(profile->file.dfa, DFA_START, name, &cond,
				     rstate);
		if (COMBINED_PERM_MASK(perms) & AA_MAY_CHANGE_PROFILE)
			return perms;
	}

	/* try matching with namespace name and then profile */
	state = aa_dfa_match(profile->file.dfa, DFA_START, ns->base.name);
	state = aa_dfa_null_transition(profile->file.dfa, state);
	return aa_str_perms(profile->file.dfa, state, name, &cond, rstate);
}

/*
 * TODO: fix parser to detect unconfined, inherit,
 * check for next name in list of names that is double null terminated
 * The names list is a set of strings that \0 seperated with a double
 * \0 terminating the list
 * names that belong to namespaces begin with a :
 * and are followed by a name a \0 seperated name.  If the name is
 * unspecified it is 0 length.  This double \0\0 does not count as
 * the end of the list
 *
 * profile\0\0			# single profile
 * profile\0profile\0\0		# 2 profiles in list
 * :namespace\0profile\0\0	# profile & namespace
 * :namespace\0\0\0		# namespace without profile
 * :namespace\0\0profile\0\0	# namespace without profile followed by profile
*/
static const char *next_name(int xtype, const char *name)
{
/* TODO: fix parser and enable
	if (xtype == AA_X_TABLE) {
		name = name + strlen(name) + 1;
		if (*name != 0)
			return name;
	}
*/
	return NULL;
}

/*
 * get target profile for xindex
 */
static struct aa_profile *x_to_profile(struct aa_namespace *ns,
				       struct aa_profile *profile,
				       const char *name, u16 xindex)

{
	struct aa_profile *new_profile = NULL;
	u16 xtype = xindex & AA_X_TYPE_MASK;
	int index = xindex & AA_X_INDEX_MASK;

	if (!profile)
		profile = ns->unconfined;

	switch(xtype) {
	case AA_X_NONE:
		/* fail exec unless ix || ux fallback - handled by caller */
		return ERR_PTR(-EACCES);
	case AA_X_NAME:
		if (xindex & AA_X_CHILD)
			new_profile = aa_sys_find_attach(&profile->base, name);
		else
			new_profile = aa_sys_find_attach(&ns->base, name);

		goto out;
	case AA_X_TABLE:
		if (index > profile->file.trans.size) {
			AA_ERROR("Invalid named transition\n");
			return ERR_PTR(-EACCES);
		}
		name = profile->file.trans.table[index];
		break;
	}

	for (; !new_profile && name; name = next_name(xtype, name)) {
		struct aa_namespace *new_ns;
		const char *xname = NULL;

		new_ns = NULL;
		if (xindex & AA_X_CHILD) {
			new_profile = aa_find_child(profile, name);
			if (new_profile)
				return new_profile;
			continue;
		} else if (*name == ':') {
			/* switching namespace */
			const char *ns_name = name + 1;
			name = xname = ns_name + strlen(ns_name) + 1;
			if (!*xname)
				/* no name so use profile name */
				xname = profile->fqname;
			if (*ns_name == '@') {
				/* TODO: variable support */
				;
			}
			new_ns = aa_find_namespace(ns_name);
			if (!new_ns)
				continue;
		} else if (*name == '@') {
			/* TODO: variable support */

		} else {
			xname = name;
		}

		new_profile = aa_find_profile_by_fqname(new_ns ? new_ns : ns,
							xname);
		aa_put_namespace(new_ns);
	}

out:
	if (!new_profile)
		return ERR_PTR(-ENOENT);

	return new_profile;
}

int apparmor_bprm_set_creds(struct linux_binprm *bprm)
{
	struct aa_task_context *cxt;
	struct aa_profile *profile, *new_profile = NULL;
	struct aa_namespace *ns;
	char *buffer = NULL;
	unsigned int state = DFA_START;
	struct aa_audit_file sa;
	struct path_cond cond = { bprm->file->f_path.dentry->d_inode->i_uid,
				  bprm->file->f_path.dentry->d_inode->i_mode };

	sa.base.error = cap_bprm_set_creds(bprm);
	if (sa.base.error)
		return sa.base.error;

	if (bprm->cred_prepared)
		return 0;

	memset(&sa, 0, sizeof(sa));
	sa.base.operation = "exec";
	sa.base.gfp_mask = GFP_KERNEL;
	sa.request = MAY_EXEC;
	sa.cond = &cond;

	cxt = bprm->cred->security;
	BUG_ON(!cxt);

	profile = aa_filtered_profile(aa_profile_newest(cxt->sys.profile));
	ns = cxt->sys.profile->ns;

	sa.base.error = aa_get_name(&bprm->file->f_path, 0, &buffer,
				    (char **) &sa.name);
	if (sa.base.error) {
		if (profile || profile->flags & PFLAG_IX_ON_NAME_ERROR)
			sa.base.error = 0;
		sa.base.info = "Exec failed name resolution";
		sa.name = bprm->filename;
		goto audit;
	}

	if (!profile) {
		/* unconfined task - attach profile if one matches */
		new_profile = aa_sys_find_attach(&ns->base, sa.name);
		if (!new_profile)
			goto cleanup;
		goto apply;
	} else if (cxt->sys.onexec) {
		/*
		 * onexec permissions are stored in a pair, rewalk the
		 * dfa to get start of the exec path match.
		 */
		sa.perms = change_profile_perms(profile, cxt->sys.onexec->ns,
						sa.name, &state);
		state = aa_dfa_null_transition(profile->file.dfa, state);
	}
	sa.perms = aa_str_perms(profile->file.dfa, state, sa.name, &cond,
				NULL);
	if (cxt->sys.onexec && sa.perms.allowed & AA_MAY_ONEXEC) {
		new_profile = cxt->sys.onexec;
		cxt->sys.onexec = NULL;
		sa.base.info = "change_profile onexec";
	} else if (sa.perms.allowed & MAY_EXEC) {
		new_profile = x_to_profile(ns, profile, sa.name,
					   sa.perms.xindex);
		if (IS_ERR(new_profile)) {
			if (sa.perms.xindex & AA_X_INHERIT) {
				/* (p|c|n)ix - don't change profile */
				sa.base.info = "ix fallback";
				goto x_clear;
			} else if (sa.perms.xindex & AA_X_UNCONFINED) {
				new_profile = aa_get_profile(ns->unconfined);
				sa.base.info = "ux fallback";
			} else {
				sa.base.error = PTR_ERR(new_profile);
				if (sa.base.error == -ENOENT)
					sa.base.info = "profile not found";
				new_profile = NULL;
			}
		}
	} else if (PROFILE_COMPLAIN(profile)) {
		new_profile = aa_alloc_null_profile(profile, 0);
		sa.base.error = -EACCES;
		if (!new_profile)
			sa.base.error = -ENOMEM;
		sa.name2 = new_profile->fqname;
		sa.perms.xindex |= AA_X_UNSAFE;
	} else {
		sa.base.error = -EACCES;
	}

	if (!new_profile)
		goto audit;

	if (profile == new_profile) {
		aa_put_profile(new_profile);
		goto audit;
	}

	if (bprm->unsafe & LSM_UNSAFE_SHARE) {
		/* FIXME: currently don't mediate shared state */
		;
	}

	if (bprm->unsafe & (LSM_UNSAFE_PTRACE | LSM_UNSAFE_PTRACE_CAP)) {
		sa.base.error = aa_may_change_ptraced_domain(current,
							     new_profile);
		if (sa.base.error)
			goto audit;
	}

	/* Determine if secure exec is needed.
	 * Can be at this point for the following reasons:
	 * 1. unconfined switching to confined
	 * 2. confined switching to different confinement
	 * 3. confined switching to unconfined
	 *
	 * Cases 2 and 3 are marked as requiring secure exec
	 * (unless policy specified "unsafe exec")
	 *
	 * bprm->unsafe is used to cache the AA_X_UNSAFE permission
	 * to avoid having to recompute in secureexec
	 */
	if (!(sa.perms.xindex & AA_X_UNSAFE))
		bprm->unsafe |= AA_SECURE_X_NEEDED;

apply:
	sa.name2 = new_profile->fqname;
	/* When switching namespace ensure its part of audit message */
	if (new_profile->ns != ns)
		sa.name3 = new_profile->ns->base.name;

	/* when transitioning profiles clear unsafe personality bits */
	bprm->per_clear |= PER_CLEAR_ON_SETID;

	aa_put_profile(cxt->sys.profile);
	cxt->sys.profile = new_profile;

x_clear:
	aa_put_profile(cxt->sys.previous);
	aa_put_profile(cxt->sys.onexec);
	cxt->sys.previous = NULL;
	cxt->sys.onexec = NULL;
	cxt->sys.token = 0;

audit:
	sa.base.error = aa_audit_file(profile, &sa);

cleanup:
	kfree(buffer);

	return sa.base.error;
}

int apparmor_bprm_secureexec(struct linux_binprm *bprm)
{
	int ret = cap_bprm_secureexec(bprm);

	/* the decision to use secure exec is computed in set_creds
	 * and stored in bprm->unsafe.  The AppArmor X_UNSAFE flag is
	 * indicates don't
	 */
	if (!ret && (bprm->unsafe & AA_SECURE_X_NEEDED))
		ret = 1;

	return ret;
}


static int aa_revalidate_perm(struct aa_profile *profile, struct file *file,
			      char *buffer, int size)
{
	umode_t mode = file->f_path.dentry->d_inode->i_mode;
	char *name;
	int error;

	error = aa_get_name_to_buffer(&file->f_path, S_ISDIR(mode), buffer,
				      size, &name);
	return aa_file_common_perm(profile, "file_inherit", file,
				   aa_map_file_to_perms(file), name,
				   error);
}

static void revalidate_file(struct aa_profile *profile, struct file *file,
			    unsigned long i, char *buffer, int size,
			    struct cred *cred)
{
	if (aa_revalidate_perm(profile, file, buffer, size)) {
		struct file *devnull = NULL;
		int fd = get_unused_fd();
		sys_close(i);
		if (fd != i) {
			if (fd >= 0)
				put_unused_fd(fd);
			return;
		}
		if (devnull) {
			get_file(devnull);
		} else if (apparmorfs_null) {
			devnull = dentry_open(dget(apparmorfs_null),
					      mntget(apparmorfs_mnt),
					      O_RDWR, cred);
			if (IS_ERR(devnull)) {
				devnull = NULL;
				put_unused_fd(fd);
				return;
			}
		} else {
			/* apparmorfs_null not setup */
			put_unused_fd(fd);
			return;
		}
		fd_install(fd, devnull);
	}
}

/*
 * derived from security/selinux/hooks.c: flush_unauthorized_files &&
 * fs/exec.c:flush_old_files
 */
static int revalidate_files(struct aa_profile *profile,
			    struct files_struct *files, gfp_t gfp,
			    struct cred *cred)
{
	struct file *file;
	struct fdtable *fdt;
	long j = -1;
	char *buffer = kmalloc(g_apparmor_path_max, gfp);
	if (!buffer)
		return -ENOMEM;

	spin_lock(&files->file_lock);
	for (;;) {
		unsigned long set, i;

		j++;
		i = j * __NFDBITS;
		fdt = files_fdtable(files);
		if (i >= fdt->max_fds)
			break;
		set = fdt->open_fds->fds_bits[j];
		if (!set)
			continue;
		spin_unlock(&files->file_lock);
		for ( ; set ; i++,set >>= 1) {
			if (set & 1) {
				file = fget(i);
				if (!file)
					continue;
				revalidate_file(profile, file, i, buffer,
						g_apparmor_path_max, cred);
				fput(file);
			}
		}
		spin_lock(&files->file_lock);
	}
	spin_unlock(&files->file_lock);
	kfree(buffer);
	return 0;
}

int apparmor_bprm_committing_creds(struct linux_binprm *bprm)
{
	struct aa_profile *profile;
	struct cred *cred = aa_get_task_policy(current, &profile);
	struct aa_task_context *new_cxt = bprm->cred->security;
	int error;

	if ((new_cxt->sys.profile == profile) ||
	    (new_cxt->sys.profile->flags & PFLAG_UNCONFINED)) {
		put_cred(cred);
		return 0;
	}
	put_cred(cred);

	error = revalidate_files(new_cxt->sys.profile, current->files,
				 GFP_KERNEL, bprm->cred);
	if (error)
		return error;

	current->pdeath_signal = 0;

	/* reset soft limits and set hard limits for the new profile */
	__aa_transition_rlimits(profile, new_cxt->sys.profile);
	return 0;
}

void apparmor_bprm_committed_creds(struct linux_binprm *bprm)
{
	/* TODO: cleanup signals - ipc mediation */
	return;
}

/**
 * aa_change_hat - change hat to/from subprofile
 * @hat_name: hat to change to
 * @token: magic value to validate the hat change
 * @permtest: true if this is just a permission test
 *
 * Change to new @hat_name, and store the @hat_magic in the current task
 * context.  If the new @hat_name is %NULL and the @token matches that
 * stored in the current task context and is not 0, return to the top level
 * profile.
 * Returns %0 on success, error otherwise.
 */
int aa_change_hat(const char *hat_name, u64 token, int permtest)
{
	const struct cred *cred;
	struct aa_task_context *cxt;
	struct aa_profile *profile, *previous_profile, *hat = NULL;
	struct aa_audit_file sa;

	memset(&sa, 0, sizeof(sa));
	sa.base.gfp_mask = GFP_KERNEL;
	sa.base.operation = "change_hat";

	cred = aa_current_policy(&profile);
	cxt = cred->security;
	previous_profile = cxt->sys.previous;
	token = cxt->sys.token;

	if (!profile) {
		sa.base.info = "unconfined";
		sa.base.error = -EPERM;
		goto audit;
	}

	if (hat_name) {
		if (previous_profile)
			sa.name = previous_profile->fqname;
		else
			sa.name = profile->fqname;

		sa.name2 = profile->ns->base.name;

		if (PROFILE_IS_HAT(profile))
			hat = aa_find_child(profile->parent, hat_name);
		else
			hat = aa_find_child(profile, hat_name);
		if (!hat) {
			sa.base.info = "hat not found";
			sa.base.error = -ENOENT;
			if (permtest || !PROFILE_COMPLAIN(profile))
				goto audit;
			hat = aa_alloc_null_profile(profile, 1);
			if (!hat) {
				sa.base.info = "failed null profile create";
				sa.base.error = -ENOMEM;
				goto audit;
			}
		} else if (!PROFILE_IS_HAT(hat)) {
			sa.base.info = "target not hat";
			sa.base.error = -EPERM;
			goto audit;
		}

		sa.base.error = aa_may_change_ptraced_domain(current, hat);
		if (sa.base.error) {
			sa.base.info = "ptraced";
			sa.base.error = -EPERM;
			goto audit;
		}

		if (!permtest) {
			sa.base.error = aa_set_current_hat(hat, token);
			if (sa.base.error == -EACCES) {
				(void)send_sig_info(SIGKILL, NULL, current);
				sa.base.error = aa_audit(AUDIT_APPARMOR_KILL,
							 profile, &sa.base,
							 file_audit_cb);
				goto out;
			}
		}
	} else if (previous_profile)
		sa.base.error = aa_restore_previous_profile(token);
	/* else
		 ignore restores when there is no saved profile
	*/

audit:
	if (!permtest)
		sa.base.error = aa_audit_file(profile, &sa);


out:
	aa_put_profile(hat);

	return sa.base.error;
}

/**
 * aa_change_profile - perform a one-way profile transition
 * @ns_name: name of the profile namespace to change to
 * @fqname: name of profile to change to
 * @onexec: whether this transition is to take place immediately or at exec
 * @permtest: true if this is just a permission test
 *
 * Change to new profile @name.  Unlike with hats, there is no way
 * to change back.  If @onexec then the transition is delayed until
 * the next exec.
 *
 * Returns %0 on success, error otherwise.
 */
int aa_change_profile(const char *ns_name, const char *fqname, int onexec,
		      int permtest)
{
	const struct cred *cred;
	struct aa_task_context *cxt;
	struct aa_profile *profile, *target = NULL;
	struct aa_namespace *ns = NULL;
	struct aa_audit_file sa;

	if (!fqname && !ns_name)
		return -EINVAL;

	memset(&sa, 0, sizeof(sa));
	sa.request = AA_MAY_CHANGE_PROFILE;
	sa.base.gfp_mask = GFP_KERNEL;
	if (onexec)
		sa.base.operation = "change_onexec";
	else
		sa.base.operation = "change_profile";

	cred = aa_current_policy(&profile);
	cxt = cred->security;

	if (ns_name) {
		sa.name2 = ns_name;
		ns = aa_find_namespace(ns_name);
		if (!ns) {
			/* we don't create new namespace in complain mode */
			sa.base.info = "namespace not found";
			sa.base.error = -ENOENT;
			goto audit;
		}
	} else {
		ns = aa_get_namespace(cxt->sys.profile->ns);
		sa.name2 = ns->base.name;
	}

	/* if the name was not specified, use the name of the current profile */
	if (!fqname) {
		if (!profile)
			fqname = ns->unconfined->fqname;
		else
			fqname = profile->fqname;
	}
	sa.name = fqname;

	sa.perms = change_profile_perms(profile, ns, fqname, NULL);
	if (!(sa.perms.allowed & AA_MAY_CHANGE_PROFILE)) {
		sa.base.error = -EACCES;
		goto audit;
	}

	target = aa_find_profile_by_fqname(ns, fqname);
	if (!target) {
		sa.base.info = "profile not found";
		sa.base.error = -ENOENT;
		if (permtest || !PROFILE_COMPLAIN(profile))
			goto audit;
		target = aa_alloc_null_profile(profile, 0);
	}

	/* check if tracing task is allowed to trace target domain */
	sa.base.error = aa_may_change_ptraced_domain(current, target);
	if (sa.base.error) {
		sa.base.info = "ptrace prevents transition";
		goto audit;
	}

	if (permtest)
		goto audit;

	if (onexec)
		sa.base.error = aa_set_current_onexec(target);
	else
		sa.base.error = aa_replace_current_profiles(target);

audit:
	if (!permtest)
		sa.base.error = aa_audit_file(profile, &sa);

	aa_put_namespace(ns);
	aa_put_profile(target);

	return sa.base.error;
}
