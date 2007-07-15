/*
 *	Copyright (C) 1998-2007 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	AppArmor /proc/pid/attr handling
 */

#include "apparmor.h"
#include "inline.h"

int aa_getprocattr(struct aa_profile *profile, char **string, unsigned *len)
{
	char *str;

	if (profile) {
		const char *mode_str = PROFILE_COMPLAIN(profile) ?
			" (complain)" : " (enforce)";
		int mode_len, name_len;

		mode_len = strlen(mode_str);
		name_len = strlen(profile->name);
		*len = mode_len + name_len + 1;
		str = kmalloc(*len, GFP_ATOMIC);
		if (!str)
			return -ENOMEM;

		memcpy(str, profile->name, name_len);
		str += name_len;
		memcpy(str, mode_str, mode_len);
		str += mode_len;
		*str++ = '\n';
		str -= *len;
	} else {
		const char *unconfined_str = "unconfined\n";

		*len = strlen(unconfined_str);
		str = kmalloc(*len, GFP_ATOMIC);
		if (!str)
			return -ENOMEM;

		memcpy(str, unconfined_str, *len);
	}
	*string = str;

	return 0;
}

static char *split_token_from_name(const char *op, char *args, u64 *cookie)
{
	char *name;

	*cookie = simple_strtoull(args, &name, 16);
	if ((name == args) || *name != '^') {
		AA_ERROR("%s: Invalid input '%s'", op, args);
		return ERR_PTR(-EINVAL);
	}

	name++;  /* skip ^ */
	if (!*name)
		name = NULL;
	return name;
}

int aa_setprocattr_changehat(char *args)
{
	char *hat;
	u64 cookie;

	hat = split_token_from_name("change_hat", args, &cookie);
	if (IS_ERR(hat))
		return PTR_ERR(hat);

	if (!hat && !cookie) {
		AA_ERROR("change_hat: Invalid input, NULL hat and NULL magic");
		return -EINVAL;
	}

	AA_DEBUG("%s: Magic 0x%llx Hat '%s'\n",
		 __FUNCTION__, cookie, hat ? hat : NULL);

	return aa_change_hat(hat, cookie);
}

int aa_setprocattr_changeprofile(char *args)
{
	char *name;
	u64 cookie;

	name = split_token_from_name("change_profile", args, &cookie);
	if (IS_ERR(name))
		return PTR_ERR(name);

	return aa_change_profile(name, cookie);
}

int aa_setprocattr_setprofile(struct task_struct *task, char *args)
{
	struct aa_profile *old_profile, *new_profile;
	struct aa_audit sa;

	memset(&sa, 0, sizeof(sa));
	sa.operation = "profile_set";
	sa.gfp_mask = GFP_KERNEL;
	sa.task = task->pid;

	AA_DEBUG("%s: current %d\n",
		 __FUNCTION__, current->pid);

repeat:
	if (strcmp(args, "unconfined") == 0)
		new_profile = NULL;
	else {
		new_profile = aa_find_profile(args);
		if (!new_profile) {
			sa.name = args;
			sa.info = "unknown profile";
			aa_audit_reject(NULL, &sa);
			return -EINVAL;
		}
	}

	old_profile = __aa_replace_profile(task, new_profile);
	if (IS_ERR(old_profile)) {
		int error;

		aa_put_profile(new_profile);
		error = PTR_ERR(old_profile);
		if (error == -ESTALE)
			goto repeat;
		return error;
	}

	if (new_profile) {
		sa.name = args;
		sa.name2 = old_profile ? old_profile->name :
			"unconfined";
		aa_audit_status(NULL, &sa);
	} else {
		if (old_profile) {
			sa.name = "unconfined";
			sa.name2 = old_profile->name;
			aa_audit_status(NULL, &sa);
		} else {
			sa.info = "task is unconfined";
			aa_audit_status(NULL, &sa);
		}
	}
	aa_put_profile(old_profile);
	aa_put_profile(new_profile);
	return 0;
}
