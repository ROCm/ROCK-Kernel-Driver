/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2004, 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <asm/uaccess.h>

#include "masklog.h"

struct mlog_bits mlog_and_bits = MLOG_BITS_RHS(MLOG_INITIAL_AND_MASK);
EXPORT_SYMBOL_GPL(mlog_and_bits);
struct mlog_bits mlog_not_bits = MLOG_BITS_RHS(MLOG_INITIAL_NOT_MASK);
EXPORT_SYMBOL_GPL(mlog_not_bits);

static char *mlog_bit_names[MLOG_MAX_BITS];

static void *mlog_name_from_pos(loff_t *caller_pos)
{
	loff_t pos = *caller_pos;
	while (pos < ARRAY_SIZE(mlog_bit_names) && mlog_bit_names[pos] == NULL)
		pos++;

	if (pos >= ARRAY_SIZE(mlog_bit_names))
		return NULL;

	*caller_pos = pos;
	return &mlog_bit_names[pos];
}

static void *mlog_seq_start(struct seq_file *seq, loff_t *pos)
{
	return mlog_name_from_pos(pos);
}

static void *mlog_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	(*pos)++;
	return mlog_name_from_pos(pos);
}

static int mlog_seq_show(struct seq_file *seq, void *v)
{
	char **name = v;
	int bit = name - mlog_bit_names;
	char *state;

	if (__mlog_test_u64((u64)1 << bit, mlog_and_bits))
		state = "allow";
	else if (__mlog_test_u64((u64)1 << bit, mlog_not_bits))
		state = "deny";
	else
		state = "off";

	seq_printf(seq, "%s %s\n", *name, state);
	return 0;
}

static void mlog_seq_stop(struct seq_file *p, void *v)
{
}

static struct seq_operations mlog_seq_ops = {
	.start = mlog_seq_start,
	.next = mlog_seq_next,
	.stop = mlog_seq_stop,
	.show = mlog_seq_show,
};

static int mlog_fop_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &mlog_seq_ops);
}

static ssize_t mlog_fop_write(struct file *filp, const char __user *buf,
			      size_t count, loff_t *pos)
{
	char *name;
	char str[32], *mask, *val;
	unsigned i, masklen, namelen;

	if (count == 0)
		return 0;

	/* count at least mask + space + 3 for "off" */
	if (*pos != 0 || count < 5 || count >= sizeof(str))
		return -EINVAL;

	if (copy_from_user(str, buf, count))
		return -EFAULT;

	str[count] = '\0';

	mask = str;
	val = strchr(str, ' ');
	if (val == NULL)
		return -EINVAL;
	*val = '\0';
	val++;

	if (strlen(val) == 0)
		return -EINVAL;

	masklen = strlen(mask);

	for (i = 0; i < ARRAY_SIZE(mlog_bit_names); i++) {
		name = mlog_bit_names[i];

		if (name == NULL)
			continue;

		namelen = strlen(name);

		if (namelen != masklen
		    || strnicmp(mask, name, namelen))
			continue;
		break;
	}
	if (i == ARRAY_SIZE(mlog_bit_names))
		return -EINVAL;

	if (!strnicmp(val, "allow", 5)) {
		__mlog_set_u64((u64)1 << i, mlog_and_bits);
		__mlog_clear_u64((u64)1 << i, mlog_not_bits);
	} else if (!strnicmp(val, "deny", 4)) {
		__mlog_set_u64((u64)1 << i, mlog_not_bits);
		__mlog_clear_u64((u64)1 << i, mlog_and_bits);
	} else if (!strnicmp(val, "off", 3)) {
		__mlog_clear_u64((u64)1 << i, mlog_not_bits);
		__mlog_clear_u64((u64)1 << i, mlog_and_bits);
	} else
		return -EINVAL;

	*pos += count;
	return count;
}

static struct file_operations mlog_seq_fops = {
	.owner = THIS_MODULE,
	.open = mlog_fop_open,
	.read = seq_read,
	.write = mlog_fop_write,
	.llseek = seq_lseek,
	.release = seq_release,
};

#define set_a_string(which) do {					\
	struct mlog_bits _bits = {{0,}, };				\
	int _bit;							\
	__mlog_set_u64(ML_##which, _bits);				\
	_bit = find_first_bit(_bits.words, MLOG_MAX_BITS);		\
	mlog_bit_names[_bit] = #which;					\
} while (0)

#define LOGMASK_PROC_NAME "log_mask"

void mlog_remove_proc(struct proc_dir_entry *parent)
{
	remove_proc_entry(LOGMASK_PROC_NAME, parent);
}

int mlog_init_proc(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *p;

	set_a_string(ENTRY);
	set_a_string(EXIT);
	set_a_string(TCP);
	set_a_string(MSG);
	set_a_string(SOCKET);
	set_a_string(HEARTBEAT);
	set_a_string(HB_BIO);
	set_a_string(DLMFS);
	set_a_string(DLM);
	set_a_string(DLM_DOMAIN);
	set_a_string(DLM_THREAD);
	set_a_string(DLM_MASTER);
	set_a_string(DLM_RECOVERY);
	set_a_string(AIO);
	set_a_string(JOURNAL);
	set_a_string(DISK_ALLOC);
	set_a_string(SUPER);
	set_a_string(FILE_IO);
	set_a_string(EXTENT_MAP);
	set_a_string(DLM_GLUE);
	set_a_string(BH_IO);
	set_a_string(UPTODATE);
	set_a_string(NAMEI);
	set_a_string(INODE);
	set_a_string(VOTE);
	set_a_string(DCACHE);
	set_a_string(CONN);
	set_a_string(QUORUM);
	set_a_string(EXPORT);
	set_a_string(ERROR);
	set_a_string(NOTICE);
	set_a_string(KTHREAD);

	p = create_proc_entry(LOGMASK_PROC_NAME, S_IRUGO, parent);
	if (p == NULL)
		return -ENOMEM;

	p->proc_fops = &mlog_seq_fops;

	return 0;
}
EXPORT_SYMBOL_GPL(mlog_init_proc);
