/* $Id: hysdn_procfs.c,v 1.1 2000/02/10 19:45:18 werner Exp $

 * Linux driver for HYSDN cards, /proc/net filesystem log functions.
 * written by Werner Cornelius (werner@titro.de) for Hypercope GmbH
 *
 * Copyright 1999  by Werner Cornelius (werner@titro.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Log: hysdn_procfs.c,v $
 * Revision 1.1  2000/02/10 19:45:18  werner
 *
 * Initial release
 *
 *
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/version.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>

#include "hysdn_defs.h"

static char *hysdn_procfs_revision = "$Revision: 1.1 $";

#define INFO_OUT_LEN 80		/* length of info line including lf */

/*************************************************/
/* structure keeping ascii log for device output */
/*************************************************/
struct log_data {
	struct log_data *next;
	ulong usage_cnt;	/* number of files still to work */
	void *proc_ctrl;	/* pointer to own control procdata structure */
	char log_start[2];	/* log string start (final len aligned by size) */
};

/**********************************************/
/* structure holding proc entrys for one card */
/**********************************************/
struct procdata {
	struct proc_dir_entry *log;	/* log entry */
	char log_name[15];	/* log filename */
	struct log_data *log_head, *log_tail;	/* head and tail for queue */
	int if_used;		/* open count for interface */
	wait_queue_head_t rd_queue;
};

/********************************************/
/* put an log buffer into the log queue.    */
/* This buffer will be kept until all files */
/* opened for read got the contents.        */
/* Flushes buffers not longer in use.       */
/********************************************/
void
put_log_buffer(hysdn_card * card, char *cp)
{
	struct log_data *ib;
	struct procdata *pd = card->procfs;
	int flags;

	if (!pd)
		return;
	if (!cp)
		return;
	if (!*cp)
		return;
	if (pd->if_used <= 0)
		return;		/* no open file for read */

	if (!(ib = (struct log_data *) kmalloc(sizeof(struct log_data) + strlen(cp), GFP_ATOMIC)))
		 return;	/* no memory */
	strcpy(ib->log_start, cp);	/* set output string */
	ib->next = NULL;
	ib->proc_ctrl = pd;	/* point to own control structure */
	save_flags(flags);
	cli();
	ib->usage_cnt = pd->if_used;
	if (!pd->log_head)
		pd->log_head = ib;	/* new head */
	else
		pd->log_tail->next = ib;	/* follows existing messages */
	pd->log_tail = ib;	/* new tail */
	restore_flags(flags);

	/* delete old entrys */
	while (pd->log_head->next) {
		if ((pd->log_head->usage_cnt <= 0) &&
		    (pd->log_head->next->usage_cnt <= 0)) {
			ib = pd->log_head;
			pd->log_head = pd->log_head->next;
			kfree(ib);
		} else
			break;
	}			/* pd->log_head->next */
	wake_up_interruptible(&(pd->rd_queue));		/* announce new entry */
}				/* put_log_buffer */


/*************************/
/* dummy file operations */
/*************************/
static loff_t
hysdn_dummy_lseek(struct file *file, loff_t offset, int orig)
{
	return -ESPIPE;
}				/* hysdn_dummy_lseek */

/**********************************/
/* log file operations and tables */
/**********************************/

/****************************************/
/* write log file -> set log level bits */
/****************************************/
static ssize_t
hysdn_log_write(struct file *file, const char *buf, size_t count, loff_t * off)
{
	int retval;
	hysdn_card *card = (hysdn_card *) file->private_data;

	if (&file->f_pos != off)	/* fs error check */
		return (-ESPIPE);

	if ((retval = pof_boot_write(card, buf, count)) < 0)
		retval = -EFAULT;	/* an error occured */

	return (retval);
}				/* hysdn_log_write */

/******************/
/* read log file */
/******************/
static ssize_t
hysdn_log_read(struct file *file, char *buf, size_t count, loff_t * off)
{
	struct log_data *inf;
	int len;
	word ino;
	struct procdata *pd;
	hysdn_card *card;

	if (!*((struct log_data **) file->private_data)) {
		if (file->f_flags & O_NONBLOCK)
			return (-EAGAIN);

		/* sorry, but we need to search the card */
		ino = file->f_dentry->d_inode->i_ino & 0xFFFF;	/* low-ino */
		card = card_root;
		while (card) {
			pd = card->procfs;
			if (pd->log->low_ino == ino)
				break;
			card = card->next;	/* search next entry */
		}
		if (card)
			interruptible_sleep_on(&(pd->rd_queue));
		else
			return (-EAGAIN);

	}
	if (!(inf = *((struct log_data **) file->private_data)))
		return (0);

	inf->usage_cnt--;	/* new usage count */
	(struct log_data **) file->private_data = &inf->next;	/* next structure */
	if ((len = strlen(inf->log_start)) <= count) {
		if (copy_to_user(buf, inf->log_start, len))
			return -EFAULT;
		file->f_pos += len;
		return (len);
	}
	return (0);
}				/* hysdn_log_read */

/******************/
/* open log file */
/******************/
static int
hysdn_log_open(struct inode *ino, struct file *filep)
{
	hysdn_card *card;
	struct procdata *pd;
	ulong flags;

	lock_kernel();
	card = card_root;
	while (card) {
		pd = card->procfs;
		if (pd->log->low_ino == (ino->i_ino & 0xFFFF))
			break;
		card = card->next;	/* search next entry */
	}
	if (!card) {
		unlock_kernel();
		return (-ENODEV);	/* device is unknown/invalid */
	}
	filep->private_data = card;	/* remember our own card */

	if ((filep->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_WRITE) {
		/* write only access -> boot pof data */
		if (pof_boot_open(card)) {
			unlock_kernel();
			return (-EPERM);	/* no permission this time */
		}
	} else if ((filep->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ) {

		/* read access -> log/debug read */
		save_flags(flags);
		cli();
		pd->if_used++;
		if (pd->log_head)
			(struct log_data **) filep->private_data = &(pd->log_tail->next);
		else
			(struct log_data **) filep->private_data = &(pd->log_head);
		restore_flags(flags);

	} else {		/* simultaneous read/write access forbidden ! */
		unlock_kernel();
		return (-EPERM);	/* no permission this time */
	}
	unlock_kernel();
	return (0);
}				/* hysdn_log_open */

/*******************************************************************************/
/* close a cardlog file. If the file has been opened for exclusive write it is */
/* assumed as pof data input and the pof loader is noticed about.              */
/* Otherwise file is handled as log output. In this case the interface usage   */
/* count is decremented and all buffers are noticed of closing. If this file   */
/* was the last one to be closed, all buffers are freed.                       */
/*******************************************************************************/
static int
hysdn_log_close(struct inode *ino, struct file *filep)
{
	struct log_data *inf;
	struct procdata *pd;
	hysdn_card *card;
	int flags, retval = 0;

	lock_kernel();
	if ((filep->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_WRITE) {
		/* write only access -> write debug completely written */
		retval = 0;	/* success */
	} else {
		/* read access -> log/debug read, mark one further file as closed */

		pd = NULL;
		save_flags(flags);
		cli();
		inf = *((struct log_data **) filep->private_data);	/* get first log entry */
		if (inf)
			pd = (struct procdata *) inf->proc_ctrl;	/* still entries there */
		else {
			/* no info available -> search card */
			card = card_root;
			while (card) {
				pd = card->procfs;
				if (pd->log->low_ino == (ino->i_ino & 0xFFFF))
					break;
				card = card->next;	/* search next entry */
			}
			if (card)
				pd = card->procfs;	/* pointer to procfs ctrl */
		}
		if (pd)
			pd->if_used--;	/* decrement interface usage count by one */

		while (inf) {
			inf->usage_cnt--;	/* decrement usage count for buffers */
			inf = inf->next;
		}
		restore_flags(flags);

		if (pd)
			if (pd->if_used <= 0)	/* delete buffers if last file closed */
				while (pd->log_head) {
					inf = pd->log_head;
					pd->log_head = pd->log_head->next;
					kfree(inf);
				}
	}			/* read access */

	unlock_kernel();
	return (retval);
}				/* hysdn_log_close */

/*************************************************/
/* select/poll routine to be able using select() */
/*************************************************/
static unsigned int
hysdn_log_poll(struct file *file, poll_table * wait)
{
	unsigned int mask = 0;
	word ino;
	hysdn_card *card;
	struct procdata *pd;

	if ((file->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_WRITE)
		return (mask);	/* no polling for write supported */

	/* we need to search the card */
	ino = file->f_dentry->d_inode->i_ino & 0xFFFF;	/* low-ino */
	card = card_root;
	while (card) {
		pd = card->procfs;
		if (pd->log->low_ino == ino)
			break;
		card = card->next;	/* search next entry */
	}
	if (!card)
		return (mask);	/* card not found */

	poll_wait(file, &(pd->rd_queue), wait);

	if (*((struct log_data **) file->private_data))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}				/* hysdn_log_poll */

/**************************************************/
/* table for log filesystem functions defined above. */
/**************************************************/
static struct file_operations log_fops =
{
	llseek:		hysdn_dummy_lseek,
	read:		hysdn_log_read,
	write:		hysdn_log_write,
	poll:		hysdn_log_poll,
	open:		hysdn_log_open,
	release:	hysdn_log_close,
};

/*****************************************/
/* Output info data to the cardinfo file */
/*****************************************/
static int
info_read(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
	char tmp[INFO_OUT_LEN * 11 + 2];
	int i;
	char *cp;
	hysdn_card *card;

	sprintf(tmp, "id bus slot type irq iobase plx-mem    dp-mem     boot device");
	cp = tmp;		/* start of string */
	while (*cp)
		cp++;
	while (((cp - tmp) % (INFO_OUT_LEN + 1)) != INFO_OUT_LEN)
		*cp++ = ' ';
	*cp++ = '\n';

	card = card_root;	/* start of list */
	while (card) {
		sprintf(cp, "%d  %3d %4d %4d %3d 0x%04x 0x%08x 0x%08x",
			card->myid,
			card->bus,
			PCI_SLOT(card->devfn),
			card->brdtype,
			card->irq,
			card->iobase,
			card->plxbase,
			card->membase);
		card = card->next;
		while (*cp)
			cp++;
		while (((cp - tmp) % (INFO_OUT_LEN + 1)) != INFO_OUT_LEN)
			*cp++ = ' ';
		*cp++ = '\n';
	}

	i = cp - tmp;
	*start = buffer;
	if (offset + length > i) {
		length = i - offset;
		*eof = 1;
	} else if (offset > i) {
		length = 0;
		*eof = 1;
	}
	cp = tmp + offset;

	if (length > 0) {
		/*   start_bh_atomic(); */
		memcpy(buffer, cp, length);
		/* end_bh_atomic(); */
		return length;
	}
	return 0;
}				/* info_read */

/*****************************/
/* hysdn subdir in /proc/net */
/*****************************/
static struct proc_dir_entry *hysdn_proc_entry = NULL;
static struct proc_dir_entry *hysdn_info_entry = NULL;

/***************************************************************************************/
/* hysdn_procfs_init is called when the module is loaded and after the cards have been */
/* detected. The needed proc dir and card entries are created.                         */
/***************************************************************************************/
int
hysdn_procfs_init(void)
{
	struct procdata *pd;
	hysdn_card *card;

	hysdn_proc_entry = create_proc_entry(PROC_SUBDIR_NAME, S_IFDIR | S_IRUGO | S_IXUGO, proc_net);
	if (!hysdn_proc_entry) {
		printk(KERN_ERR "HYSDN: unable to create hysdn subdir\n");
		return (-1);
	}
	hysdn_info_entry = create_proc_entry("cardinfo", 0, hysdn_proc_entry);
	if (hysdn_info_entry)
		hysdn_info_entry->read_proc = info_read;	/* read info function */

	/* create all cardlog proc entries */

	card = card_root;	/* start with first card */
	while (card) {
		if ((pd = (struct procdata *) kmalloc(sizeof(struct procdata), GFP_KERNEL)) != NULL) {
			memset(pd, 0, sizeof(struct procdata));

			sprintf(pd->log_name, "%s%d", PROC_LOG_BASENAME, card->myid);
			if ((pd->log = create_proc_entry(pd->log_name, S_IFREG | S_IRUGO | S_IWUSR, hysdn_proc_entry)) != NULL) {
				pd->log->proc_fops = &log_fops;	/* set new operations table */
				pd->log->owner = THIS_MODULE;
			}

			init_waitqueue_head(&(pd->rd_queue));

			card->procfs = (void *) pd;	/* remember procfs structure */
		}
		card = card->next;	/* point to next card */
	}

	printk(KERN_NOTICE "HYSDN: procfs Rev. %s initialised\n", hysdn_getrev(hysdn_procfs_revision));
	return (0);
}				/* hysdn_procfs_init */

/***************************************************************************************/
/* hysdn_procfs_release is called when the module is unloaded and before the cards     */
/* resources are released. The module counter is assumed to be 0 !                     */
/***************************************************************************************/
void
hysdn_procfs_release(void)
{
	struct procdata *pd;
	hysdn_card *card;

	card = card_root;	/* start with first card */
	while (card) {
		if ((pd = (struct procdata *) card->procfs) != NULL) {
			if (pd->log)
				remove_proc_entry(pd->log_name, hysdn_proc_entry);
			kfree(pd);	/* release memory */
		}
		card = card->next;	/* point to next card */
	}

	remove_proc_entry("cardinfo", hysdn_proc_entry);
	remove_proc_entry(PROC_SUBDIR_NAME, proc_net);
}				/* hysdn_procfs_release */
