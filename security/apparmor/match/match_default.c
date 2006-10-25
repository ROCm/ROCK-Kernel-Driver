/*
 *	Copyright (C) 2002-2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	http://forge.novell.com/modules/xfmod/project/?apparmor
 *
 *	AppArmor default match submodule (literal and tailglob)
 */

#include <linux/module.h>
#include "match.h"

static const char *features="literal tailglob";

void* aamatch_alloc(enum entry_match_type type)
{
	return NULL;
}

void aamatch_free(void *ptr)
{
}

const char *aamatch_features(void)
{
	return features;
}

int aamatch_serialize(void *entry_extradata, struct aa_ext *e,
		      aamatch_serializecb cb)
{
	return 0;
}

unsigned int aamatch_match(const char *pathname, const char *entry_name,
			   enum entry_match_type type, void *entry_extradata)
{
	int ret;

	ret = aamatch_match_common(pathname, entry_name, type);

	return ret;
}

EXPORT_SYMBOL_GPL(aamatch_alloc);
EXPORT_SYMBOL_GPL(aamatch_free);
EXPORT_SYMBOL_GPL(aamatch_features);
EXPORT_SYMBOL_GPL(aamatch_serialize);
EXPORT_SYMBOL_GPL(aamatch_match);

MODULE_DESCRIPTION("AppArmor match module (aamatch) [default]");
MODULE_AUTHOR("Tony Jones <tonyj@suse.de>");
MODULE_LICENSE("GPL");
