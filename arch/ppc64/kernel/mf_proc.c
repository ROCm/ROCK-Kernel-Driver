/*
 * mf_proc.c
 * Copyright (C) 2001 Kyle A. Lucke  IBM Corporation
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/iSeries/mf.h>

static int proc_mf_dump_cmdline(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len = count;
	char *p;

	if (off) {
		*eof = 1;
		return 0;
	}

	len = mf_getCmdLine(page, &len, (u64)data);
   
	p = page;
	while (len < (count - 1)) {
		if (!*p || *p == '\n')
			break;
		p++;
		len++;
	}
	*p = '\n';
	p++;
	*p = 0;

	return p - page;
}

#if 0
static int proc_mf_dump_vmlinux(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int sizeToGet = count;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (mf_getVmlinuxChunk(page, &sizeToGet, off, (u64)data) == 0) {
		if (sizeToGet != 0) {
			*start = page + off;
			return sizeToGet;
		}
		*eof = 1;
		return 0;
	}
	*eof = 1;
	return 0;
}
#endif

static int proc_mf_dump_side(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len;
	char mf_current_side = mf_getSide();

	len = sprintf(page, "%c\n", mf_current_side);

	if (len <= (off + count))
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;			
}

static int proc_mf_change_side(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	char stkbuf[10];

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (count > (sizeof(stkbuf) - 1))
		count = sizeof(stkbuf) - 1;
	if (copy_from_user(stkbuf, buffer, count))
		return -EFAULT;
	stkbuf[count] = 0;
	if ((*stkbuf != 'A') && (*stkbuf != 'B') &&
	    (*stkbuf != 'C') && (*stkbuf != 'D')) {
		printk(KERN_ERR "mf_proc.c: proc_mf_change_side: invalid side\n");
		return -EINVAL;
	}

	mf_setSide(*stkbuf);

	return count;
}

static int proc_mf_dump_src(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len;

	mf_getSrcHistory(page, count);
	len = count;
	len -= off;			
	if (len < count) {		
		*eof = 1;		
		if (len <= 0)		
			return 0;	
	} else				
		len = count;		
	*start = page + off;		
	return len;			
}

static int proc_mf_change_src(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	char stkbuf[10];

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if ((count < 4) && (count != 1)) {
		printk(KERN_ERR "mf_proc: invalid src\n");
		return -EINVAL;
	}

	if (count > (sizeof(stkbuf) - 1))
		count = sizeof(stkbuf) - 1;
	if (copy_from_user(stkbuf, buffer, count))
		return -EFAULT;

	if ((count == 1) && (*stkbuf == '\0'))
		mf_clearSrc();
	else
		mf_displaySrc(*(u32 *)stkbuf);

	return count;			
}

static int proc_mf_change_cmdline(struct file *file, const char *buffer,
		unsigned long count, void *data)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	mf_setCmdLine(buffer, count, (u64)data);

	return count;			
}

static int proc_mf_change_vmlinux(struct file *file, const char *buffer,
		unsigned long count, void *data)
{
	int rc;
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	rc = mf_setVmlinuxChunk(buffer, count, file->f_pos, (u64)data);
	if (rc < 0)
		return rc;

	file->f_pos += count;

	return count;			
}

static int __init mf_proc_init(void)
{
	struct proc_dir_entry *mf_proc_root;
	struct proc_dir_entry *ent;
	struct proc_dir_entry *mf;
	char name[2];
	int i;

	mf_proc_root = proc_mkdir("iSeries/mf", NULL);
	if (!mf_proc_root)
		return 1;

	name[1] = '\0';
	for (i = 0; i < 4; i++) {
		name[0] = 'A' + i;
		mf = proc_mkdir(name, mf_proc_root);
		if (!mf)
			return 1;

		ent = create_proc_entry("cmdline", S_IFREG|S_IRUSR|S_IWUSR, mf);
		if (!ent)
			return 1;
		ent->nlink = 1;
		ent->data = (void *)(long)i;
		ent->read_proc = proc_mf_dump_cmdline;
		ent->write_proc = proc_mf_change_cmdline;

		if (i == 3)	/* no vmlinux entry for 'D' */
			continue;

		ent = create_proc_entry("vmlinux", S_IFREG|S_IWUSR, mf);
		if (!ent)
			return 1;
		ent->nlink = 1;
		ent->data = (void *)(long)i;
#if 0
		if (i == 3) {
			/*
			 * if we had a 'D' vmlinux entry, it would only
			 * be readable.
			 */
			ent->read_proc = proc_mf_dump_vmlinux;
			ent->write_proc = NULL;
		} else
#endif
		{
			ent->write_proc = proc_mf_change_vmlinux;
			ent->read_proc = NULL;
		}
	}

	ent = create_proc_entry("side", S_IFREG|S_IRUSR|S_IWUSR, mf_proc_root);
	if (!ent)
		return 1;
	ent->nlink = 1;
	ent->data = (void *)0;
	ent->read_proc = proc_mf_dump_side;
	ent->write_proc = proc_mf_change_side;

	ent = create_proc_entry("src", S_IFREG|S_IRUSR|S_IWUSR, mf_proc_root);
	if (!ent)
		return 1;
	ent->nlink = 1;
	ent->data = (void *)0;
	ent->read_proc = proc_mf_dump_src;
	ent->write_proc = proc_mf_change_src;

	return 0;
}

__initcall(mf_proc_init);
