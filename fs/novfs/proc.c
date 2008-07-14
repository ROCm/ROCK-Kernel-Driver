/*
 * Novell NCP Redirector for Linux
 * Author: James Turner
 *
 * This module contains functions that create the interface to the proc
 * filesystem.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>

#include "vfs.h"

struct proc_dir_entry *novfs_procfs_dir;
struct proc_dir_entry *Novfs_Control;
struct proc_dir_entry *Novfs_Library;
struct proc_dir_entry *Novfs_Version;

static struct file_operations novfs_daemon_proc_fops;
static struct file_operations novfs_lib_proc_fops;

/*===[ Code ]=============================================================*/

static int Novfs_Get_Version(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *buf, tbuf[48];
	int len = 0, i;

	if (!off) {
		buf = page + off;
		*start = buf;
		len = sprintf(buf, "Novfs Version=%s\n", NOVFS_VERSION_STRING);
		i = novfs_daemon_getversion(tbuf, sizeof(tbuf));
		if ((i > 0) && i < (count - len)) {
			len += sprintf(buf + len, "Novfsd Version=%s\n", tbuf);
		}

		if (novfs_current_mnt) {
			i = strlen(novfs_current_mnt);
			if ((i > 0) && i < (count - len)) {
				len +=
				    sprintf(buf + len, "Novfs mount=%s\n",
					    novfs_current_mnt);
			}
		}
		DbgPrint("Novfs_Get_Version:\n%s\n", buf);
	}
	*eof = 1;
	return (len);
}

int novfs_proc_init(void)
{
	int retCode = 0;

	novfs_procfs_dir = proc_mkdir(MODULE_NAME, NULL);
	if (novfs_procfs_dir) {
		novfs_procfs_dir->owner = THIS_MODULE;

		Novfs_Control = create_proc_entry("Control", 0600, novfs_procfs_dir);

		if (Novfs_Control) {
			Novfs_Control->owner = THIS_MODULE;
			Novfs_Control->size = 0;
			memcpy(&novfs_daemon_proc_fops,
					Novfs_Control->proc_fops,
					sizeof(struct file_operations));

			/*
			 * Setup our functions
			 */
			novfs_daemon_proc_fops.owner = THIS_MODULE;
			novfs_daemon_proc_fops.open = novfs_daemon_open_control;
			novfs_daemon_proc_fops.release = novfs_daemon_close_control;
			novfs_daemon_proc_fops.read = novfs_daemon_cmd_send;
			novfs_daemon_proc_fops.write = novfs_daemon_recv_reply;
			novfs_daemon_proc_fops.ioctl = novfs_daemon_ioctl;

			Novfs_Control->proc_fops = &novfs_daemon_proc_fops;
		} else {
			remove_proc_entry(MODULE_NAME, NULL);
			return (-ENOENT);
		}

		Novfs_Library = create_proc_entry("Library", 0666, novfs_procfs_dir);
		if (Novfs_Library) {
			Novfs_Library->owner = THIS_MODULE;
			Novfs_Library->size = 0;

			/*
			 * Setup our file functions
			 */
			memcpy(&novfs_lib_proc_fops, Novfs_Library->proc_fops,
			       sizeof(struct file_operations));
			novfs_lib_proc_fops.owner = THIS_MODULE;
			novfs_lib_proc_fops.open = novfs_daemon_lib_open;
			novfs_lib_proc_fops.release = novfs_daemon_lib_close;
			novfs_lib_proc_fops.read = novfs_daemon_lib_read;
			novfs_lib_proc_fops.write = novfs_daemon_lib_write;
			novfs_lib_proc_fops.llseek = novfs_daemon_lib_llseek;
			novfs_lib_proc_fops.ioctl = novfs_daemon_lib_ioctl;
			Novfs_Library->proc_fops = &novfs_lib_proc_fops;
		} else {
			remove_proc_entry("Control", novfs_procfs_dir);
			remove_proc_entry(MODULE_NAME, NULL);
			return (-ENOENT);
		}

		Novfs_Version =
		    create_proc_read_entry("Version", 0444, novfs_procfs_dir,
					   Novfs_Get_Version, NULL);
		if (Novfs_Version) {
			Novfs_Version->owner = THIS_MODULE;
			Novfs_Version->size = 0;
		} else {
			remove_proc_entry("Library", novfs_procfs_dir);
			remove_proc_entry("Control", novfs_procfs_dir);
			remove_proc_entry(MODULE_NAME, NULL);
			retCode = -ENOENT;
		}
	} else {
		retCode = -ENOENT;
	}
	return (retCode);
}

void novfs_proc_exit(void)
{

	DbgPrint("Uninit_Procfs_Interface remove_proc_entry(Version, NULL)\n");
	remove_proc_entry("Version", novfs_procfs_dir);

	DbgPrint("Uninit_Procfs_Interface remove_proc_entry(Control, NULL)\n");
	remove_proc_entry("Control", novfs_procfs_dir);

	DbgPrint("Uninit_Procfs_Interface remove_proc_entry(Library, NULL)\n");
	remove_proc_entry("Library", novfs_procfs_dir);

	DbgPrint("Uninit_Procfs_Interface remove_proc_entry(%s, NULL)\n",
		 MODULE_NAME);
	remove_proc_entry(MODULE_NAME, NULL);

	DbgPrint("Uninit_Procfs_Interface done\n");
}
