/*
 *	Copyright (C) 2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	AppArmor /proc/pid/attr handling
 */

/* for isspace */
#include <linux/ctype.h>
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

int aa_setprocattr_changehat(char *hatinfo, size_t infosize)
{
	int error = -EINVAL;
	char *token = NULL, *hat;
	u64 magic;

	AA_DEBUG("%s: %p %zd\n", __FUNCTION__, hatinfo, infosize);

	/* strip leading white space */
	while (infosize && isspace(*hatinfo)) {
		hatinfo++;
		infosize--;
	}

	if (infosize == 0)
		return -EINVAL;

	/*
	 * Copy string to a new buffer so we can play with it
	 * It may be zero terminated but we add a trailing 0
	 * for 100% safety
	 */
	token = kmalloc(infosize + 1, GFP_KERNEL);
	if (!token)
		return -ENOMEM;
	memcpy(token, hatinfo, infosize);
	token[infosize] = 0;

	magic = simple_strtoull(token, &hat, 16);
	if (hat == token || *hat != '^') {
		AA_WARN(GFP_KERNEL, "%s: Invalid input '%s'\n",
			__FUNCTION__, token);
		goto out;
	}

	/* skip ^ */
	hat++;

	if (!*hat)
		hat = NULL;

	if (!hat && !magic) {
		AA_WARN(GFP_KERNEL,
			"%s: Invalid input, NULL hat and NULL magic\n",
			__FUNCTION__);
		goto out;
	}

	AA_DEBUG("%s: Magic 0x%llx Hat '%s'\n",
		 __FUNCTION__, magic, hat ? hat : NULL);

	error = aa_change_hat(hat, magic);

out:
	if (token) {
		memset(token, 0, infosize);
		kfree(token);
	}

	return error;
}

int aa_setprocattr_setprofile(struct task_struct *task, char *name, size_t size)
{
	struct aa_profile *old_profile, *new_profile;
	char *name_copy = NULL;
	int error;

	AA_DEBUG("%s: current %s(%d)\n",
		 __FUNCTION__, current->comm, current->pid);

	/* strip leading white space */
	while (size && isspace(*name)) {
		name++;
		size--;
	}
	if (size == 0)
		return -EINVAL;

	/* Create a zero-terminated copy if the name. */
	name_copy = kmalloc(size + 1, GFP_KERNEL);
	if (!name_copy)
		return -ENOMEM;

	strncpy(name_copy, name, size);
	name_copy[size] = 0;

repeat:
	if (strcmp(name_copy, "unconfined") != 0) {
		new_profile = aa_find_profile(name_copy);
		if (!new_profile) {
			AA_WARN(GFP_KERNEL,
				"%s: Unable to switch task %s(%d) to profile"
				"'%s'. No such profile.\n",
				__FUNCTION__,
				task->comm, task->pid,
				name_copy);

			error = -EINVAL;
			goto out;
		}
	} else
		new_profile = NULL;

	old_profile = aa_replace_profile(task, new_profile, 0);
	if (IS_ERR(old_profile)) {
		aa_put_profile(new_profile);
		error = PTR_ERR(old_profile);
		if (error == -ESTALE)
			goto repeat;
		goto out;
	}

	if (new_profile) {
		AA_WARN(GFP_KERNEL,
			"%s: Switching task %s(%d) "
			"profile %s active %s to new profile %s\n",
			__FUNCTION__,
			task->comm, task->pid,
			old_profile ? old_profile->parent->name :
				"unconfined",
			old_profile ? old_profile->name : "unconfined",
			name_copy);
	} else {
		if (old_profile) {
			AA_WARN(GFP_KERNEL,
				"%s: Unconfining task %s(%d) "
				"profile %s active %s\n",
				__FUNCTION__,
				task->comm, task->pid,
				old_profile->parent->name,
				old_profile->name);
		} else {
			AA_WARN(GFP_KERNEL,
				"%s: task %s(%d) "
				"is already unconfined\n",
				__FUNCTION__, task->comm, task->pid);
		}
	}

	aa_put_profile(old_profile);
	aa_put_profile(new_profile);
	error = 0;

out:
	kfree(name_copy);
	return error;
}
