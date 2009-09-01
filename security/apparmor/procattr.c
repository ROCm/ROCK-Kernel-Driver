/*
 * AppArmor security module
 *
 * This file contains AppArmor /proc/<pid>/attr/ interface functions
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
#include "include/policy.h"
#include "include/domain.h"

/* FIXME show profile multiplexing */
int aa_getprocattr(struct aa_namespace *ns, struct aa_profile *profile,
		   char **string)
{
	char *str;
	int len = 0;

	if (profile) {
		int mode_len, name_len, ns_len = 0;
		const char *mode_str = profile_mode_names[profile->mode];
		char *s;

		mode_len = strlen(mode_str) + 3;  /* _(mode_str)\n */
		name_len = strlen(profile->fqname);
		if (ns != default_namespace)
			ns_len = strlen(ns->base.name) + 3;
		len = mode_len + ns_len + name_len + 1;
		s = str = kmalloc(len + 1, GFP_ATOMIC);
		if (!str)
			return -ENOMEM;

		if (ns_len) {
			sprintf(s, "%s://", ns->base.name);
			s += ns_len;
		}
		memcpy(s, profile->fqname, name_len);
		s += name_len;
		sprintf(s, " (%s)\n", mode_str);
	} else {
		const char *unconfined_str = "unconfined\n";

		len = strlen(unconfined_str);
		if (ns != default_namespace)
			len += strlen(ns->base.name) + 1;

		str = kmalloc(len + 1, GFP_ATOMIC);
		if (!str)
			return -ENOMEM;

		if (ns != default_namespace)
			sprintf(str, "%s://%s", ns->base.name, unconfined_str);
		else
			memcpy(str, unconfined_str, len);
	}
	*string = str;

	return len;
}

static char *split_token_from_name(const char *op, char *args, u64 *token)
{
	char *name;

	*token = simple_strtoull(args, &name, 16);
	if ((name == args) || *name != '^') {
		AA_ERROR("%s: Invalid input '%s'", op, args);
		return ERR_PTR(-EINVAL);
	}

	name++;  /* skip ^ */
	if (!*name)
		name = NULL;
	return name;
}

int aa_setprocattr_changehat(char *args, int test)
{
	char *hat;
	u64 token;

	hat = split_token_from_name("change_hat", args, &token);
	if (IS_ERR(hat))
		return PTR_ERR(hat);

	if (!hat && !token) {
		AA_ERROR("change_hat: Invalid input, NULL hat and NULL magic");
		return -EINVAL;
	}

	AA_DEBUG("%s: Magic 0x%llx Hat '%s'\n",
		 __func__, token, hat ? hat : NULL);

	return aa_change_hat(hat, token, test);
}

int aa_setprocattr_changeprofile(char *args, int onexec, int test)
{
	char *name, *ns_name;

	name = aa_split_name_from_ns(args, &ns_name);
	return aa_change_profile(ns_name, name, onexec, test);
}


int aa_setprocattr_permipc(char *args)
{
	/* TODO: add ipc permission querying */
	return -ENOTSUPP;
}
