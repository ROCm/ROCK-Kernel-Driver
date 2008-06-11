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

struct proc_dir_entry *Novfs_Procfs_dir;
static struct proc_dir_entry *Novfs_Control;
static struct proc_dir_entry *Novfs_Library;
static struct proc_dir_entry *Novfs_Version;

static struct file_operations Daemon_proc_fops;
static struct file_operations Library_proc_fops;

/*===[ Code ]=============================================================*/

static int Novfs_Get_Version(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *buf, tbuf[48];
	int len = 0, i;

	if (!off) {
		buf = page + off;
		*start = buf;
		len = sprintf(buf, "Novfs Version=%s\n", NOVFS_VERSION_STRING);
		i = Daemon_getversion(tbuf, sizeof(tbuf));
		if ((i > 0) && i < (count - len)) {
			len += sprintf(buf + len, "Novfsd Version=%s\n", tbuf);
		}

		if (Novfs_CurrentMount) {
			i = strlen(Novfs_CurrentMount);
			if ((i > 0) && i < (count - len)) {
				len +=
				    sprintf(buf + len, "Novfs mount=%s\n",
					    Novfs_CurrentMount);
			}
		}
		DbgPrint("Novfs_Get_Version:\n%s\n", buf);
	}
	*eof = 1;
	return (len);
}

int Init_Procfs_Interface(void)
{
	int retCode = 0;

	Novfs_Procfs_dir = proc_mkdir(MODULE_NAME, NULL);
	if (Novfs_Procfs_dir) {
		Novfs_Procfs_dir->owner = THIS_MODULE;

		Novfs_Control = create_proc_entry("Control", 0600, Novfs_Procfs_dir);

		if (Novfs_Control) {
			Novfs_Control->owner = THIS_MODULE;
			Novfs_Control->size = 0;
			memcpy(&Daemon_proc_fops, Novfs_Control->proc_fops,
			       sizeof(struct file_operations));

			/*
			 * Setup our functions
			 */
			Daemon_proc_fops.owner = THIS_MODULE;
			Daemon_proc_fops.open = Daemon_Open_Control;
			Daemon_proc_fops.release = Daemon_Close_Control;
			Daemon_proc_fops.read = Daemon_Send_Command;
			Daemon_proc_fops.write = Daemon_Receive_Reply;
			Daemon_proc_fops.ioctl = Daemon_ioctl;

			Novfs_Control->proc_fops = &Daemon_proc_fops;
		} else {
			remove_proc_entry(MODULE_NAME, NULL);
			return (-ENOENT);
		}

		Novfs_Library = create_proc_entry("Library", 0666, Novfs_Procfs_dir);
		if (Novfs_Library) {
			Novfs_Library->owner = THIS_MODULE;
			Novfs_Library->size = 0;

			/*
			 * Setup our file functions
			 */
			memcpy(&Library_proc_fops, Novfs_Library->proc_fops,
			       sizeof(struct file_operations));
			Library_proc_fops.owner = THIS_MODULE;
			Library_proc_fops.open = Daemon_Library_open;
			Library_proc_fops.release = Daemon_Library_close;
			Library_proc_fops.read = Daemon_Library_read;
			Library_proc_fops.write = Daemon_Library_write;
			Library_proc_fops.llseek = Daemon_Library_llseek;
			Library_proc_fops.ioctl = Daemon_Library_ioctl;
			Novfs_Library->proc_fops = &Library_proc_fops;
		} else {
			remove_proc_entry("Control", Novfs_Procfs_dir);
			remove_proc_entry(MODULE_NAME, NULL);
			return (-ENOENT);
		}

		Novfs_Version =
		    create_proc_read_entry("Version", 0444, Novfs_Procfs_dir,
					   Novfs_Get_Version, NULL);
		if (Novfs_Version) {
			Novfs_Version->owner = THIS_MODULE;
			Novfs_Version->size = 0;
		} else {
			remove_proc_entry("Library", Novfs_Procfs_dir);
			remove_proc_entry("Control", Novfs_Procfs_dir);
			remove_proc_entry(MODULE_NAME, NULL);
			retCode = -ENOENT;
		}
	} else {
		retCode = -ENOENT;
	}
	return (retCode);
}

void Uninit_Procfs_Interface(void)
{

	DbgPrint("Uninit_Procfs_Interface remove_proc_entry(Version, NULL)\n");
	remove_proc_entry("Version", Novfs_Procfs_dir);

	DbgPrint("Uninit_Procfs_Interface remove_proc_entry(Control, NULL)\n");
	remove_proc_entry("Control", Novfs_Procfs_dir);

	DbgPrint("Uninit_Procfs_Interface remove_proc_entry(Library, NULL)\n");
	remove_proc_entry("Library", Novfs_Procfs_dir);

	DbgPrint("Uninit_Procfs_Interface remove_proc_entry(%s, NULL)\n",
		 MODULE_NAME);
	remove_proc_entry(MODULE_NAME, NULL);

	DbgPrint("Uninit_Procfs_Interface done\n");
}
