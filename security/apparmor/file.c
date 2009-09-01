/*
 * AppArmor security module
 *
 * This file contains AppArmor mediation of files
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include "include/security/apparmor.h"
#include "include/audit.h"
#include "include/file.h"
#include "include/match.h"
#include "include/path.h"
#include "include/policy.h"

struct file_perms nullperms;

static void aa_audit_file_sub_mask(struct audit_buffer *ab, char *buffer,
				   u16 mask, u16 xindex)
{

  /*	const char xchar[] = "PpCc";*/

	char *m = buffer;

	if (mask & AA_EXEC_MMAP)
		*m++ = 'm';
	if (mask & MAY_READ)
		*m++ = 'r';
	if (mask & (MAY_WRITE | AA_MAY_CREATE))
		*m++ = 'w';
	else if (mask & MAY_APPEND)
		*m++ = 'a';
	if (mask & AA_MAY_LINK)
		*m++ = 'l';
	if (mask & AA_MAY_LOCK)
		*m++ = 'k';
	if (mask & MAY_EXEC) {
		*m++ = 'x';

/* FIXME: only want more advanced auditing of x if in audit/hint mode
		u16 index = xindex & AA_X_INDEX_MASK;
		u16 xtype = xindex & AA_X_TYPE_MASK;
		if (xtype > AA_X_NONE)
			*m++ = xchar[(xindex >> 12) & 0x3];
		if (xindex & AA_X_INHERIT) {
			*m++ = 'i';
		} else if (xindex & AA_X_UNCONFINED) {
			if (xindex & AA_X_UNSAFE)
				*m++ = 'u';
			else
				*m++ = 'U';
		}
		*m++ = 'x';
		/ * at most 7 character including trailing \0 * /
		if (xtype == AA_X_VARIABLE) {
			m += sprintf(m, "->v%x", index);
		} else if (xtype == AA_X_TABLE) {
			m += sprintf(m, "->n%x", index);
		}
*/
	}
	*m++ = '\0';
}

static void aa_audit_file_mask(struct audit_buffer *ab, const char *name,
			       u16 mask, int xindex, int owner)
{
/*	char str[18]; */
	char str[10];

	aa_audit_file_sub_mask(ab, str, mask, xindex);
	if (owner)
		audit_log_format(ab, " %s=\"%s::\"", name, str);
	else
		audit_log_format(ab, " %s=\"::%s\"", name, str);
}

void file_audit_cb(struct audit_buffer *ab, void *va)
{
	struct aa_audit_file *sa = va;
	u16 denied = sa->request & ~sa->perms.allowed;
	uid_t fsuid;

	if (sa->base.task)
		fsuid = task_uid(sa->base.task);
	else
		fsuid = current_fsuid();

	if (sa->request & AA_AUDIT_FILE_MASK)
		aa_audit_file_mask(ab, "requested_mask", sa->request,
				   AA_X_NONE, fsuid == sa->cond->uid);

	if (denied & AA_AUDIT_FILE_MASK)
		aa_audit_file_mask(ab, "denied_mask", denied, sa->perms.xindex,
			fsuid == sa->cond->uid);

	if (sa->request & AA_AUDIT_FILE_MASK) {
		audit_log_format(ab, " fsuid=%d", fsuid);
		audit_log_format(ab, " ouid=%d", sa->cond->uid);
	}

	if (sa->name) {
		audit_log_format(ab, " name=");
		audit_log_untrustedstring(ab, sa->name);
	}

	if (sa->name2) {
		audit_log_format(ab, " name2=");
		audit_log_untrustedstring(ab, sa->name2);
	}

	if (sa->name3) {
		audit_log_format(ab, " name3=");
		audit_log_untrustedstring(ab, sa->name3);
	}
}

int aa_audit_file(struct aa_profile *profile, struct aa_audit_file *sa)
{
	int type = AUDIT_APPARMOR_AUTO;

	if (likely(!sa->base.error)) {
		u16 mask = sa->perms.audit;

		if (unlikely(PROFILE_AUDIT_MODE(profile) == AUDIT_ALL))
			mask = 0xffff;

		/* mask off perms that are not being force audited */
		sa->request &= mask;

		if (likely(!sa->request))
			return 0;
		type = AUDIT_APPARMOR_AUDIT;
	} else {
		/* quiet auditing of specific known rejects */
		u16 mask = sa->perms.quiet;
		u16 denied = sa->request & ~sa->perms.allowed;

		if (denied & sa->perms.kill)
			type = AUDIT_APPARMOR_KILL;

		/* assumes quiet and kill do not overlap */
		if ((denied & mask) &&
		    PROFILE_AUDIT_MODE(profile) != AUDIT_NOQUIET &&
		    PROFILE_AUDIT_MODE(profile) != AUDIT_ALL)
			sa->request &= ~mask;

		if (!sa->request)
			return PROFILE_COMPLAIN(profile) ? 0 : sa->base.error;
	}
	return aa_audit(type, profile, (struct aa_audit *)sa, file_audit_cb);
}

/* FIXME: convert from dfa + state to permission entry */
struct file_perms aa_compute_perms(struct aa_dfa *dfa, unsigned int state,
				   struct path_cond *cond)
{
	struct file_perms perms;

	/* FIXME: change over to new dfa format */
	/* currently file perms are encoded in the dfa */
	perms.kill = 0;
	perms.dindex = 0;

	if (current_fsuid() == cond->uid) {
		perms.allowed = dfa_user_allow(dfa, state);
		perms.audit = dfa_user_audit(dfa, state);
		perms.quiet = dfa_user_quiet(dfa, state);
		perms.xindex = dfa_user_xindex(dfa, state);
	} else {
		perms.allowed = dfa_other_allow(dfa, state);
		perms.audit = dfa_other_audit(dfa, state);
		perms.quiet = dfa_other_quiet(dfa, state);
		perms.xindex = dfa_other_xindex(dfa, state);
	}
	/* in the old mapping MAY_WRITE implies AA_MAY_CREATE */
	perms.allowed |= (perms.allowed & MAY_WRITE) << 6;
	perms.audit |= (perms.audit & MAY_WRITE) << 6;
	perms.quiet |= (perms.quiet & MAY_WRITE) << 6;

	/* in the old mapping AA_MAY_LOCK and link subset are overlayed
	 * and only determined by which part of a pair they are  in
	 */
	if (perms.allowed & AA_MAY_LOCK)
		perms.allowed |= AA_LINK_SUBSET;

	/* change_profile wasn't determined by ownership in old mapping */
	if (ACCEPT_TABLE(dfa)[state] & 0x80000000)
		perms.allowed |= AA_MAY_CHANGE_PROFILE;

	return perms;
}

struct file_perms aa_str_perms(struct aa_dfa *dfa, unsigned int start,
			       const char *name, struct path_cond *cond,
			       unsigned int *rstate)
{
	unsigned int state;
	if (!dfa)
		return nullperms;

	state = aa_dfa_match(dfa, start, name);

	if (rstate)
		*rstate = state;

	/* TODO: convert to new dfa format */

	return aa_compute_perms(dfa, state, cond);
}

int aa_pathstr_perm(struct aa_profile *profile, const char *op,
		    const char *name, u16 request, struct path_cond *cond)
{
	struct aa_audit_file sa;

	memset(&sa, 0, sizeof(sa));
	sa.base.operation = op;
	sa.base.gfp_mask = GFP_KERNEL;
	sa.request = request;
	sa.name = name;
	sa.cond = cond;

	sa.perms = aa_str_perms(profile->file.dfa, DFA_START, sa.name, cond,
				NULL);
	if (request & ~sa.perms.allowed)
		sa.base.error = -EACCES;
	return aa_audit_file(profile, &sa);
}

int aa_path_perm(struct aa_profile *profile, const char *operation,
		 struct path *path, u16 request, struct path_cond *cond)
{
	struct aa_audit_file sa;
	char *buffer, *name;

	memset(&sa, 0, sizeof(sa));
	sa.base.operation = operation;
	sa.base.gfp_mask = GFP_KERNEL;
	sa.request = request;
	sa.cond = cond;

	sa.base.error = aa_get_name(path, S_ISDIR(cond->mode), &buffer,
				    &name);
	sa.name = name;
	if (sa.base.error) {
		sa.perms = nullperms;
		if (sa.base.error == -ENOENT)
			sa.base.info = "Failed name lookup - deleted entry";
		else if (sa.base.error == -ESTALE)
			sa.base.info = "Failed name lookup - disconnected path";
		else if (sa.base.error == -ENAMETOOLONG)
			sa.base.info = "Failed name lookup - name too long";
		else
			sa.base.info = "Failed name lookup";
	} else {
		sa.perms = aa_str_perms(profile->file.dfa, DFA_START, sa.name,
					cond, NULL);
		if (request & ~sa.perms.allowed)
			sa.base.error = -EACCES;
	}
	sa.base.error = aa_audit_file(profile, &sa);
	kfree(buffer);

	return sa.base.error;
}

int aa_path_link(struct aa_profile *profile, struct dentry *old_dentry,
		 struct path *new_dir, struct dentry *new_dentry)
{
	struct path link = { new_dir->mnt, new_dentry };
	struct path target = { new_dir->mnt, old_dentry };
	struct path_cond cond = { old_dentry->d_inode->i_uid,
				  old_dentry->d_inode->i_mode };
	char *buffer = NULL, *buffer2 = NULL;
	char *lname, *tname;
	struct file_perms perms;
	unsigned int state;

	struct aa_audit_file sa;
	memset(&sa, 0, sizeof(sa));
	sa.base.operation = "link";
	sa.base.gfp_mask = GFP_KERNEL;
	sa.request = AA_MAY_LINK;
	sa.cond = &cond;
	sa.perms = nullperms;

	sa.base.error = aa_get_name(&link, 0, &buffer, &lname);
	sa.name = lname;
	if (sa.base.error)
		goto audit;

	sa.base.error = aa_get_name(&target, 0, &buffer2, &tname);
	sa.name2 = tname;
	if (sa.base.error)
		goto audit;


	sa.perms = aa_str_perms(profile->file.dfa, DFA_START, sa.name, &cond,
			     &state);
	sa.perms.audit &= AA_MAY_LINK;
	sa.perms.quiet &= AA_MAY_LINK;
	sa.perms.kill &= AA_MAY_LINK;

	if (!(sa.perms.allowed & AA_MAY_LINK)) {
		sa.base.error = -EACCES;
		goto audit;
	}

	/* test to see if target can be paired with link */
	state = aa_dfa_null_transition(profile->file.dfa, state);
	perms = aa_str_perms(profile->file.dfa, state, sa.name2, &cond, NULL);
	if (!(perms.allowed & AA_MAY_LINK)) {
		sa.base.error = -EACCES;
		sa.base.info = "target restricted";
		goto audit;
	}

	/* done if link subset test is not required */
	if (!(perms.allowed & AA_LINK_SUBSET))
		goto audit;

	/* Do link perm subset test requiring allowed permission on link are a
	 * subset of the allowed permissions on target.
	 */
	perms = aa_str_perms(profile->file.dfa, DFA_START, sa.name2, &cond,
			     NULL);

	/* AA_MAY_LINK is not considered in the subset test */
	sa.request = sa.perms.allowed & ~AA_MAY_LINK;
	sa.perms.allowed &= perms.allowed | AA_MAY_LINK;

	sa.request |= AA_AUDIT_FILE_MASK & (sa.perms.allowed & ~perms.allowed);
	if (sa.request & ~sa.perms.allowed)
		sa.base.error = -EACCES;
	else if (sa.perms.allowed & MAY_EXEC) {
		if (((sa.perms.xindex & ~AA_X_UNSAFE) !=
		     (perms.xindex &~AA_X_UNSAFE)) ||
		    ((sa.perms.xindex & AA_X_UNSAFE) &&
		     !(perms.xindex & AA_X_UNSAFE))) {
			sa.perms.allowed &= ~MAY_EXEC;
			sa.request |= MAY_EXEC;
			sa.base.error = -EACCES;
			sa.base.info = "link not subset of target";
		}
	}

audit:
	sa.base.error = aa_audit_file(profile, &sa);
	kfree(buffer);
	kfree(buffer2);

	return sa.base.error;
}


static inline int aa_is_deleted_file(struct dentry *dentry)
{
	if (d_unhashed(dentry) && dentry->d_inode->i_nlink == 0)
		return 1;
	return 0;
}

int aa_file_common_perm(struct aa_profile *profile, const char *operation,
			struct file *file, u16 request, const char *name,
			int error)
{
	struct path_cond cond = { .uid = file->f_path.dentry->d_inode->i_uid,
				 .mode = file->f_path.dentry->d_inode->i_mode };
	struct aa_audit_file sa;

	memset(&sa, 0, sizeof(sa));
	sa.base.operation = operation;
	sa.base.gfp_mask = GFP_KERNEL;
	sa.request = request;
	sa.base.error = error;
	sa.name = name;
	sa.cond = &cond;

	if (sa.base.error) {
		sa.perms = nullperms;
		if (sa.base.error == -ENOENT &&
		    aa_is_deleted_file(file->f_path.dentry)) {
			/* Access to open files that are deleted are
			 * give a pass (implicit delegation
			 */
			sa.base.error = 0;
			sa.perms.allowed = sa.request;
		} else if (sa.base.error == -ENOENT)
			sa.base.info = "Failed name lookup - deleted entry";
		else if (sa.base.error == -ESTALE)
			sa.base.info = "Failed name lookup - disconnected path";
		else if (sa.base.error == -ENAMETOOLONG)
			sa.base.info = "Failed name lookup - name too long";
		else
			sa.base.info = "Failed name lookup";
	} else {
		sa.perms = aa_str_perms(profile->file.dfa, DFA_START, sa.name,
					&cond, NULL);
		if (request & ~sa.perms.allowed)
			sa.base.error = -EACCES;
	}
	sa.base.error = aa_audit_file(profile, &sa);

	return sa.base.error;
}

int aa_file_perm(struct aa_profile *profile, const char *operation,
		 struct file *file, u16 request)
{
	char *buffer, *name;
	umode_t mode = file->f_path.dentry->d_inode->i_mode;
	int error = aa_get_name(&file->f_path, S_ISDIR(mode), &buffer, &name);

	error = aa_file_common_perm(profile, operation, file, request, name,
				    error);
	kfree(buffer);
	return error;
}
