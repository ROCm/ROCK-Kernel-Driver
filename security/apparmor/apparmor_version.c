/*
 *	Copyright (C) 2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	AppArmor version definition
 */

#ifndef APPARMOR_VERSION
#error "-DAPPARMOR_VERSION must be specified when compiling this file"
#endif

#define APPARMOR_VERSION_STR_PFX "APPARMOR_VERSION="

#include <linux/module.h>
MODULE_VERSION(APPARMOR_VERSION);

/* apparmor_version_str exists to allow a strings on module to
 * see APPARMOR_VERSION= prefix
 */
static const char *apparmor_version_str =
		APPARMOR_VERSION_STR_PFX APPARMOR_VERSION;

/* apparmor_version_str_nl exists to allow an easy way to get a newline
 * terminated string without having to do dynamic memory allocation
 */
static const char *apparmor_version_str_nl = APPARMOR_VERSION "\n";

const char *apparmor_version(void)
{
	const int len = sizeof(APPARMOR_VERSION_STR_PFX) - 1;

	return apparmor_version_str + len;
}

const char *apparmor_version_nl(void)
{
	return apparmor_version_str_nl;
}
