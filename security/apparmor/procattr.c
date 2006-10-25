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

size_t aa_getprocattr(struct aaprofile *active, char *str, size_t size)
{
	int error = -EACCES;	/* default to a perm denied */
	size_t len;

	if (active) {
		size_t lena, lenm, lenp = 0;
		const char *enforce_str = " (enforce)";
		const char *complain_str = " (complain)";
		const char *mode_str =
			PROFILE_COMPLAIN(active) ? complain_str : enforce_str;

		lenm = strlen(mode_str);

		lena = strlen(active->name);

		len = lena;
		if (IN_SUBPROFILE(active)) {
			lenp = strlen(BASE_PROFILE(active)->name);
			len += (lenp + 1);	/* +1 for ^ */
		}
		/* DONT null terminate strings we output via proc */
		len += (lenm + 1);	/* for \n */

		if (len <= size) {
			if (lenp) {
				memcpy(str, BASE_PROFILE(active)->name,
				       lenp);
				str += lenp;
				*str++ = '^';
			}

			memcpy(str, active->name, lena);
			str += lena;
			memcpy(str, mode_str, lenm);
			str += lenm;
			*str++ = '\n';
			error = len;
		} else if (size == 0) {
			error = len;
		} else {
			error = -ERANGE;
		}
	} else {
		const char *unconstrained_str = "unconstrained\n";
		len = strlen(unconstrained_str);

		/* DONT null terminate strings we output via proc */
		if (len <= size) {
			memcpy(str, unconstrained_str, len);
			error = len;
		} else if (size == 0) {
			error = len;
		} else {
			error = -ERANGE;
		}
	}

	return error;

}

int aa_setprocattr_changehat(char *hatinfo, size_t infosize)
{
	int error = -EINVAL;
	char *token = NULL, *hat, *smagic, *tmp;
	u32 magic;
	int rc, len, consumed;
	unsigned long flags;

	AA_DEBUG("%s: %p %zd\n", __FUNCTION__, hatinfo, infosize);

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
		AA_WARN("%s: Invalid input '%s'\n", __FUNCTION__, token);
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
		AA_WARN("%s: Invalid hex magic %s\n",
			__FUNCTION__,
			smagic);
		goto out;
	}

	hat = tmp + 1;

	if (!*hat)
		hat = NULL;

	if (!hat && !magic) {
		AA_WARN("%s: Invalid input, NULL hat and NULL magic\n",
			__FUNCTION__);
		goto out;
	}

	AA_DEBUG("%s: Magic 0x%x Hat '%s'\n",
		 __FUNCTION__, magic, hat ? hat : NULL);

	spin_lock_irqsave(&sd_lock, flags);
	error = aa_change_hat(hat, magic);
	spin_unlock_irqrestore(&sd_lock, flags);

out:
	if (token) {
		memset(token, 0, infosize);
		kfree(token);
	}

	return error;
}

int aa_setprocattr_setprofile(struct task_struct *p, char *profilename,
			      size_t profilesize)
{
	int error = -EINVAL;
	struct aaprofile *profile = NULL;
	struct subdomain *sd;
	char *name = NULL;
	unsigned long flags;

	AA_DEBUG("%s: current %s(%d)\n",
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

 repeat:
	if (strcmp(name, "unconstrained") != 0) {
		profile = aa_profilelist_find(name);
		if (!profile) {
			AA_WARN("%s: Unable to switch task %s(%d) to profile"
				"'%s'. No such profile.\n",
				__FUNCTION__,
				p->comm, p->pid,
				name);

			error = -EINVAL;
			goto out;
		}
	}

	spin_lock_irqsave(&sd_lock, flags);

	sd = AA_SUBDOMAIN(p->security);

	/* switch to unconstrained */
	if (!profile) {
		if (__aa_is_confined(sd)) {
			AA_WARN("%s: Unconstraining task %s(%d) "
				"profile %s active %s\n",
				__FUNCTION__,
				p->comm, p->pid,
				BASE_PROFILE(sd->active)->name,
				sd->active->name);

			aa_switch_unconfined(sd);
		} else {
			AA_WARN("%s: task %s(%d) "
				"is already unconstrained\n",
				__FUNCTION__, p->comm, p->pid);
		}
	} else {
		if (!sd) {
			/* this task was created before module was
			 * loaded, allocate a subdomain
			 */
			AA_WARN("%s: task %s(%d) has no subdomain\n",
				__FUNCTION__, p->comm, p->pid);

			/* unlock so we can safely GFP_KERNEL */
			spin_unlock_irqrestore(&sd_lock, flags);

			sd = alloc_subdomain(p);
			if (!sd) {
				AA_WARN("%s: Unable to allocate subdomain for "
					"task %s(%d). Cannot confine task to "
					"profile %s\n",
					__FUNCTION__,
					p->comm, p->pid,
					name);

				error = -ENOMEM;
				put_aaprofile(profile);

				goto out;
			}

			spin_lock_irqsave(&sd_lock, flags);
			if (!AA_SUBDOMAIN(p->security)) {
				p->security = sd;
			} else { /* race */
				free_subdomain(sd);
				sd = AA_SUBDOMAIN(p->security);
			}
		}

		/* ensure the profile hasn't been replaced */

		if (unlikely(profile->isstale)) {
			WARN_ON(profile == null_complain_profile);

			/* drop refcnt obtained from earlier get_aaprofile */
			put_aaprofile(profile);
			profile = aa_profilelist_find(name);

			if (!profile) {
				/* Race, profile was removed. */
				spin_unlock_irqrestore(&sd_lock, flags);
				goto repeat;
			}
		}

		/* we do not do a normal task replace since we are not
		 * replacing with the same profile.
		 * If existing process is in a hat, it will be moved
		 * into the new parent profile, even if this new
		 * profile has a identical named hat.
		 */

		AA_WARN("%s: Switching task %s(%d) "
			"profile %s active %s to new profile %s\n",
			__FUNCTION__,
			p->comm, p->pid,
			sd->active ? BASE_PROFILE(sd->active)->name :
				"unconstrained",
			sd->active ? sd->active->name : "unconstrained",
			name);

		aa_switch(sd, profile);

		put_aaprofile(profile); /* drop ref we obtained above
					 * from aa_profilelist_find
					 */

		/* Reset magic in case we were in a subhat before
		 * This is the only case where we zero the magic after
		 * calling aa_switch
		 */
		sd->hat_magic = 0;
	}

	spin_unlock_irqrestore(&sd_lock, flags);

	error = 0;
out:
	kfree(name);

	return error;
}
