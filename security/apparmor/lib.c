/*
 * AppArmor security module
 *
 * This file contains basic common functions used in AppArmor
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/slab.h>
#include <linux/string.h>

#include "include/audit.h"

void info_message(const char *str)
{
	struct aa_audit sa;
	memset(&sa, 0, sizeof(sa));
	sa.gfp_mask = GFP_KERNEL;
	sa.info = str;
	printk(KERN_INFO "AppArmor: %s\n", str);
	if (audit_enabled)
		aa_audit(AUDIT_APPARMOR_STATUS, NULL, &sa, NULL);
}

char *strchrnul(const char *s, int c)
{
	for (; *s != (char)c && *s != '\0'; ++s)
		;
	return (char *)s;
}

char *aa_split_name_from_ns(char *args, char **ns_name)
{
	char *name = strstrip(args);

	*ns_name  = NULL;
	if (args[0] == ':') {
		char *split = strstrip(strchr(&args[1], ':'));

		if (!split)
			return NULL;

		*split = 0;
		*ns_name = &args[1];
		name = strstrip(split + 1);
	}
	if (*name == 0)
		name = NULL;

	return name;
}

char *new_compound_name(const char *n1, const char *n2)
{
	char *name = kmalloc(strlen(n1) + strlen(n2) + 3, GFP_KERNEL);
	if (name)
		sprintf(name, "%s//%s", n1, n2);
	return name;
}

/**
 * aa_strneq - compare null terminated @str to a non null terminated substring
 * @str: a null terminated string
 * @sub: a substring, not necessarily null terminated
 * @len: length of @sub to compare
 *
 * The @str string must be full consumed for this to be considered a match
 */
int aa_strneq(const char *str, const char *sub, int len)
{
	int res = strncmp(str, sub, len);
	if (res)
		return 0;
	if (str[len] == 0)
		return 1;
	return 0;
}

const char *fqname_subname(const char *name)
{
	char *split;
	/* check for namespace which begins with a : and ends with : or \0 */
	name = strstrip((char *) name);
	if (*name == ':') {
		split = strchrnul(name + 1, ':');
		if (*split == '\0')
			return NULL;
		name = strstrip(split + 1);
	}
	for (split = strstr(name, "//"); split; split = strstr(name, "//")) {
		name = split + 2;
	}
	return name;
}
