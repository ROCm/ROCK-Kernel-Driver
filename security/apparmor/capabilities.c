/*
 *	Copyright (C) 2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	AppArmor capability definitions
 */

#include "apparmor.h"

static const char *cap_names[] = {
	"chown",
	"dac_override",
	"dac_read_search",
	"fowner",
	"fsetid",
	"kill",
	"setgid",
	"setuid",
	"setpcap",
	"linux_immutable",
	"net_bind_service",
	"net_broadcast",
	"net_admin",
	"net_raw",
	"ipc_lock",
	"ipc_owner",
	"sys_module",
	"sys_rawio",
	"sys_chroot",
	"sys_ptrace",
	"sys_pacct",
	"sys_admin",
	"sys_boot",
	"sys_nice",
	"sys_resource",
	"sys_time",
	"sys_tty_config",
	"mknod",
	"lease",
	"audit_write",
	"audit_control"
};

const char *capability_to_name(unsigned int cap)
{
	const char *name;

	name = (cap < (sizeof(cap_names) / sizeof(char *))
		   ? cap_names[cap] : "invalid-capability");

	return name;
}

static const char *syscall_names[] = {
	"ptrace",
	"sysctl (write)",
	"mount",
	"umount"
};

const char *syscall_to_name(enum aasyscall call)
{
	const char *name;
	name = (call < (sizeof(syscall_names) / sizeof(char *))
		? syscall_names[call] : "invalid-syscall");
	return name;
}
