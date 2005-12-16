/*
 *	Copyright (C) 2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	SubDomain version definition
 */

#ifndef SUBDOMAIN_VERSION
#error "-DSUBDOMAIN_VERSION must be specified when compiling this file"
#endif

#define SUBDOMAIN_VERSION_STR_PFX "SUBDOMAIN_VERSION="

#include <linux/module.h>
MODULE_VERSION(SUBDOMAIN_VERSION);

/* subdomain_version_str exists to allow a strings on module to
 * see SUBDOMAIN_VERSION= prefix
 */
static const char *subdomain_version_str =
		SUBDOMAIN_VERSION_STR_PFX SUBDOMAIN_VERSION;

/* subdomain_version_str_nl exists to allow an easy way to get a newline
 * terminated string without having to do dynamic memory allocation
 */
static const char *subdomain_version_str_nl = SUBDOMAIN_VERSION "\n";

const char *subdomain_version(void)
{
	const int len = sizeof(SUBDOMAIN_VERSION_STR_PFX) - 1;

	return subdomain_version_str + len;
}

const char *subdomain_version_nl(void)
{
	return subdomain_version_str_nl;
}
