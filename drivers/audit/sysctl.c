/*
 * sysctl.c
 *
 * Linux Audit Subsystem, handle sysctl's.
 *
 * Copyright (C) 2003 SuSE Linux AG
 *
 * Written by okir@suse.de, based on ideas from systrace, by
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/version.h>
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <linux/audit.h>

#include "audit-private.h"

static ctl_table	audit_table[] = {
	{ 1, "attach-all",
	  &audit_all_processes,
	  sizeof(audit_all_processes),
	  0600, NULL, &proc_dointvec, NULL, },
	{ 2, "allow-suspend",
	  &audit_allow_suspend,
	  sizeof(audit_allow_suspend),
	  0600, NULL,  &proc_dointvec, NULL, },
	{ 3, "max-messages",
	  &audit_max_messages,
	  sizeof(audit_max_messages),
	  0600, NULL,  &proc_dointvec, NULL, },
	{ 4, "paranoia",
	  &audit_paranoia,
	  sizeof(audit_paranoia),
	  0600, NULL,  &proc_dointvec, NULL, },
	{ 5, "debug",
	  &audit_debug,
	  sizeof(audit_debug),
	  0600, NULL,  &proc_dointvec, NULL, },
	{ 0 },
};

static ctl_table	audit_root[] = {
	{ 1, "audit", NULL, 0, 0555, audit_table },
	{ 0 }
};

static ctl_table	dev_root[] = {
	{ CTL_KERN, "dev", NULL, 0, 0555, audit_root },
	{ 0 }
};

static struct ctl_table_header *	sysctl_header = NULL;

int
audit_sysctl_register(void)
{
	sysctl_header = register_sysctl_table(dev_root, 0);
	return 0;
}

void
audit_sysctl_unregister(void)
{
	if (sysctl_header)
		unregister_sysctl_table(sysctl_header);
	sysctl_header = NULL;
}
