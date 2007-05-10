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

		*len = ((profile != profile->parent) ?
		           strlen(profile->parent->name) + 1 : 0) +
		       strlen(mode_str) + strlen(profile->name) + 1;
		str = kmalloc(*len, GFP_ATOMIC);
		if (!str)
			return -ENOMEM;

		if (profile != profile->parent) {
			memcpy(str, profile->parent->name,
			       strlen(profile->parent->name));
			str += strlen(profile->parent->name);
			*str++ = '^';
		}
		memcpy(str, profile->name, strlen(profile->name));
		str += strlen(profile->name);
		memcpy(str, mode_str, strlen(mode_str));
		str += strlen(mode_str);
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

int aa_setprocattr_changehat(char *args)
{
	char *hat;
	u64 magic;

	magic = simple_strtoull(args, &hat, 16);
	if (hat == args || *hat != '^') {
		AA_ERROR("change_hat: Invalid input '%s'", args);
		return -EINVAL;
	}
	hat++;  /* skip ^ */
	if (!*hat)
		hat = NULL;
	if (!hat && !magic) {
		AA_ERROR("change_hat: Invalid input, NULL hat and NULL magic");
		return -EINVAL;
	}

	AA_DEBUG("%s: Magic 0x%llx Hat '%s'\n",
		 __FUNCTION__, magic, hat ? hat : NULL);

	return aa_change_hat(hat, magic);
}

int aa_setprocattr_setprofile(struct task_struct *task, char *args)
{
	struct aa_profile *old_profile, *new_profile;

	AA_DEBUG("%s: current %d\n",
		 __FUNCTION__, current->pid);

repeat:
	if (strcmp(args, "unconfined") == 0)
		new_profile = NULL;
	else {
		new_profile = aa_find_profile(args);
		if (!new_profile) {
			aa_audit_message(NULL, GFP_KERNEL, "Unable to switch "
					 "task %d to profile '%s'. No such "
					 "profile.",
					 task->pid, args);

			return -EINVAL;
		}
	}

	old_profile = __aa_replace_profile(task, new_profile, 0);
	if (IS_ERR(old_profile)) {
		int error;

		aa_put_profile(new_profile);
		error = PTR_ERR(old_profile);
		if (error == -ESTALE)
			goto repeat;
		return error;
	}

	if (new_profile) {
		aa_audit_message(NULL, GFP_KERNEL, "Switching task %d profile "
				 "%s active %s to new profile %s",
				 task->pid, old_profile ?
				 old_profile->parent->name : "unconfined",
				 old_profile ? old_profile->name : "unconfined",
				 args);
	} else {
		if (old_profile) {
			aa_audit_message(NULL, GFP_KERNEL, "Unconfining task "
					 "%d profile %s active %s",
					 task->pid, old_profile->parent->name,
					 old_profile->name);
		} else {
			aa_audit_message(NULL, GFP_KERNEL, "task %d is already "
					 "unconfined",
					 task->pid);
		}
	}

	aa_put_profile(old_profile);
	aa_put_profile(new_profile);

	return 0;
}
