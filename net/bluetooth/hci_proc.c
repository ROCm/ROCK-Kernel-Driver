/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 * Bluetooth HCI Proc FS support.
 *
 * $Id: hci_proc.c,v 1.0 2002/04/17 17:37:16 maxk Exp $
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/sock.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#ifndef CONFIG_BT_HCI_CORE_DEBUG
#undef  BT_DBG
#define BT_DBG( A... )
#endif

#ifdef CONFIG_PROC_FS
struct proc_dir_entry *proc_bt_hci;

static int hci_seq_open(struct file *file, struct seq_operations *op, void *priv)
{
	struct seq_file *seq;

	if (seq_open(file, op))
		return -ENOMEM;

	seq = file->private_data;
	seq->private = priv;
	return 0;
}

static void *inq_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct hci_dev *hdev = seq->private;
	struct inquiry_entry *inq;
	loff_t l = *pos;
	
	hci_dev_lock_bh(hdev);

	for (inq = hdev->inq_cache.list; inq; inq = inq->next)
		if (!l--)
			return inq;
	return NULL;
}

static void *inq_seq_next(struct seq_file *seq, void *e, loff_t *pos)
{
	struct inquiry_entry *inq = e;
	return inq->next;
}

static void inq_seq_stop(struct seq_file *seq, void *e)
{
	struct hci_dev *hdev = seq->private;
	hci_dev_unlock_bh(hdev);
}

static int  inq_seq_show(struct seq_file *seq, void *e)
{
	struct inquiry_entry *inq = e;
	struct inquiry_info  *info = &inq->info;

	seq_printf(seq, "%s %d %d %d 0x%.2x%.2x%.2x 0x%.4x %u\n", batostr(&info->bdaddr), 
			info->pscan_rep_mode, info->pscan_period_mode, info->pscan_mode,
			info->dev_class[0], info->dev_class[1], info->dev_class[2],
			info->clock_offset, inq->timestamp);
	return 0;
}

static struct seq_operations inq_seq_ops = {
	.start  = inq_seq_start,
	.next   = inq_seq_next,
	.stop   = inq_seq_stop,
	.show   = inq_seq_show 
};

static int inq_seq_open(struct inode *inode, struct file *file)
{
	return hci_seq_open(file, &inq_seq_ops, PDE(inode)->data);
}

static struct file_operations inq_seq_fops = {
	.owner	 = THIS_MODULE,
	.open    = inq_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

int hci_dev_proc_init(struct hci_dev *hdev)
{
	struct proc_dir_entry *e;
	char id[10];

	sprintf(id, "%d", hdev->id);

	hdev->proc = proc_mkdir(id, proc_bt_hci);
	if (!hdev->proc)
		return -ENOMEM;

	e = create_proc_entry("inquiry_cache", S_IRUGO, hdev->proc);
	if (e) {
		e->proc_fops = &inq_seq_fops;
		e->data = (void *) hdev;
	}

        return 0;
}

void hci_dev_proc_cleanup(struct hci_dev *hdev)
{
	char id[10];
	sprintf(id, "%d", hdev->id);

	remove_proc_entry("inquiry_cache", hdev->proc);

	remove_proc_entry(id, proc_bt_hci);
}

int  __init hci_proc_init(void)
{
	proc_bt_hci = proc_mkdir("hci", proc_bt);
        return 0;
}

void __exit hci_proc_cleanup(void)
{
	remove_proc_entry("hci", proc_bt);
}

#else /* CONFIG_PROC_FS */

int hci_dev_proc_init(struct hci_dev *hdev)
{
        return 0;
}

void hci_dev_proc_cleanup(struct hci_dev *hdev)
{
        return;
}

int  __init hci_proc_init(void)
{
        return 0;
}

void __exit hci_proc_cleanup(void)
{
        return;
}

#endif /* CONFIG_PROC_FS */
