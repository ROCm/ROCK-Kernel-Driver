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

size_t sd_getprocattr(struct subdomain *sd, char *str, size_t size)
{
	int error = -EACCES;	/* default to a perm denied */
	size_t len;

	if (__sd_is_confined(sd)) {
		size_t lena, lenm, lenp = 0;
		const char *enforce_str = " (enforce)";
		const char *complain_str = " (complain)";
		const char *mode_str =
			SUBDOMAIN_COMPLAIN(sd) ? complain_str : enforce_str;

		lenm = strlen(mode_str);

		lena = strlen(sd->active->name);

		len = lena;
		if (sd->active != sd->profile) {
			lenp = strlen(sd->profile->name);
			len += (lenp + 1);	/* +1 for ^ */
		}
		/* DONT null terminate strings we output via proc */
		len += (lenm + 1);	/* for \n */

		if (len <= size) {
			if (lenp) {
				memcpy(str, sd->profile->name, lenp);
				str += lenp;
				*str++ = '^';
			}

			memcpy(str, sd->active->name, lena);
			str += lena;
			memcpy(str, mode_str, lenm);
			str += lenm;
			*str++ = '\n';
			error = len;
		} else {
			error = -ERANGE;
		}
	} else {
		const char *unconstrained_str = SD_UNCONSTRAINED "\n";
		len = strlen(unconstrained_str);

		/* DONT null terminate strings we output via proc */
		if (len <= size) {
			memcpy(str, unconstrained_str, len);
			error = len;
		} else {
			error = -ERANGE;
		}
	}

	return error;

}
int sd_setprocattr_changehat(char *hatinfo, size_t infosize)
{
	int error = -EINVAL;
	char *token = NULL, *hat, *smagic, *tmp;
	__u32 magic;
	int rc, len, consumed;
	unsigned long flags;

	SD_DEBUG("%s: %p %zd\n", __FUNCTION__, hatinfo, infosize);

	/* strip leading white space */
	while (infosize && isspace(*hatinfo)) {
		hatinfo++;
		infosize--;
	}

	if (infosize == 0)
		goto out;

	/*
	 * Copy string to a new buffer so we can play with it
	 * It may be zero terminated but we add a trailing 0
	 * for 100% safety
	 */
	token = kmalloc(infosize + 1, GFP_KERNEL);

	if (!token) {
		error = -ENOMEM;
		goto out;
	}

	memcpy(token, hatinfo, infosize);
	token[infosize] = 0;

	/* error is INVAL until we have at least parsed something */
	error = -EINVAL;

	tmp = token;
	while (*tmp && *tmp != '^') {
		tmp++;
	}

	if (!*tmp || tmp == token) {
		SD_WARN("%s: Invalid input '%s'\n", __FUNCTION__, token);
		goto out;
	}

	/* split magic and hat into two strings */
	*tmp = 0;
	smagic = token;

	/*
	 * Initially set consumed=strlen(magic), as if sscanf
	 * consumes all input via the %x it will not process the %n
	 * directive. Otherwise, if sscanf does not consume all the
	 * input it will process the %n and update consumed.
	 */
	consumed = len = strlen(smagic);

	rc = sscanf(smagic, "%x%n", &magic, &consumed);

	if (rc != 1 || consumed != len) {
		SD_WARN("%s: Invalid hex magic %s\n",
			__FUNCTION__,
			smagic);
		goto out;
	}

	hat = tmp + 1;

	if (!*hat)
		hat = NULL;

	if (!hat && !magic) {
		SD_WARN("%s: Invalid input, NULL hat and NULL magic\n",
			__FUNCTION__);
		goto out;
	}

	SD_DEBUG("%s: Magic 0x%x Hat '%s'\n",
		 __FUNCTION__, magic, hat ? hat : NULL);

	write_lock_irqsave(&sd_lock, flags);
	error = sd_change_hat(hat, magic);
	write_unlock_irqrestore(&sd_lock, flags);

out:
	if (token) {
		memset(token, 0, infosize);
		kfree(token);
	}

	return error;
}

int sd_setprocattr_setprofile(struct task_struct *p, char *profilename,
			      size_t profilesize)
{
	int error = -EINVAL;
	struct sdprofile *profile;
	struct subdomain *sd;
	char *name = NULL;
	unsigned long flags;

	SD_DEBUG("%s: current %s(%d)\n",
		 __FUNCTION__, current->comm, current->pid);

	/* strip leading white space */
	while (profilesize && isspace(*profilename)) {
		profilename++;
		profilesize--;
	}

	if (profilesize == 0)
		goto out;

	/*
	 * Copy string to a new buffer so we guarantee it is zero
	 * terminated
	 */
	name = kmalloc(profilesize + 1, GFP_KERNEL);

	if (!name) {
		error = -ENOMEM;
		goto out;
	}

	strncpy(name, profilename, profilesize);
	name[profilesize] = 0;

	if (strcmp(name, SD_UNCONSTRAINED) == 0)
		profile = null_profile;
	else
		profile = sd_profilelist_find(name);

	if (!profile) {
		SD_WARN("%s: Unable to switch task %s(%d) to profile '%s'. "
			"No such profile.\n",
			__FUNCTION__,
			p->comm, p->pid,
			name);

		error = -EINVAL;
		goto out;
	}


	write_lock_irqsave(&sd_lock, flags);

	sd = SD_SUBDOMAIN(p->security);

	/* switch to unconstrained */
	if (profile == null_profile) {
		if (__sd_is_confined(sd)) {
			SD_WARN("%s: Unconstraining task %s(%d) "
				"profile %s active %s\n",
				__FUNCTION__,
				p->comm, p->pid,
				sd->profile->name,
				sd->active->name);

			sd_switch_unconfined(sd);
		} else {
			SD_WARN("%s: task %s(%d) "
				"is already unconstrained\n",
				__FUNCTION__, p->comm, p->pid);
		}
	} else {
		if (!sd) {
			/* this task was created before module was
			 * loaded, allocate a subdomain
			 */
			SD_WARN("%s: task %s(%d) has no subdomain\n",
				__FUNCTION__, p->comm, p->pid);

			/* unlock so we can safely GFP_KERNEL */
			write_unlock_irqrestore(&sd_lock, flags);

			sd = alloc_subdomain(p);
			if (!sd) {
				SD_WARN("%s: Unable to allocate subdomain for "
					"task %s(%d). Cannot confine task to "
					"profile %s\n",
					__FUNCTION__,
					p->comm, p->pid,
					name);

				error = -ENOMEM;
				put_sdprofile(profile);

				goto out;
			}

			write_lock_irqsave(&sd_lock, flags);
			if (!p->security) {
				p->security = sd;
			} else { /* race */
				free_subdomain(sd);
				sd = SD_SUBDOMAIN(p->security);
			}
		}

		/* we do not do a normal task replace since we are not
		 * replacing with the same profile.
		 * If existing process is in a hat, it will be moved
		 * into the new parent profile, even if this new
		 * profile has a identical named hat.
		 */

		SD_WARN("%s: Switching task %s(%d) "
			"profile %s active %s to new profile %s\n",
			__FUNCTION__,
			p->comm, p->pid,
			sd->profile ? sd->profile->name : SD_UNCONSTRAINED,
			sd->active ? sd->profile->name : SD_UNCONSTRAINED,
			name);

		sd_switch(sd, profile, profile);

		put_sdprofile(profile); /* drop ref we obtained above
					 * from sd_profilelist_find
					 */

		/* Reset magic in case we were in a subhat before
		 * This is the only case where we zero the magic after
		 * calling sd_switch
		 */
		sd->sd_hat_magic = 0;
	}

	write_unlock_irqrestore(&sd_lock, flags);

out:
	kfree(name);

	return error;
}
