/*
 *  drivers/s390/cio/proc.c
 *   S/390 common I/O routines -- proc file system entries
 *   $Revision: 1.5 $
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *                            IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *               Cornelia Huck (cohuck@de.ibm.com) 
 *		 Arnd Bergmann (arndb@de.ibm.com)
 *    ChangeLog: 11/04/2002 Arnd Bergmann Split s390io.c into multiple files,
 *					  see s390io.c for complete list of
 * 					  changes.
 *               05/03/2002 Cornelia Huck  removed /proc/deviceinfo/
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/s390io.h>
#include <asm/debug.h>

#include "ioinfo.h"
#include "s390io.h"
#include "cio_debug.h"

static int chan_proc_init (void);

int show_interrupts(struct seq_file *p, void *v)
{
	int i, j;

	seq_puts(p, "           ");

	for (j=0; j<num_online_cpus(); j++)
		seq_printf(p, "CPU%d       ",j);

	seq_putc(p, '\n');

	for (i = 0 ; i < NR_IRQS ; i++) {
		if (ioinfo[i] == INVALID_STORAGE_AREA)
			continue;

		seq_printf(p, "%3d: ",i);
		seq_printf(p, "  %s", ioinfo[i]->irq_desc.name);

		seq_putc(p, '\n');
	
	} /* endfor */

	return 0;
}

/* 
 * Display info on subchannels in /proc/subchannels. 
 * Adapted from procfs stuff in dasd.c by Cornelia Huck, 02/28/01.      
 */

typedef struct {
	char *data;
	int len;
} tempinfo_t;

static struct proc_dir_entry *chan_subch_entry;

static int
chan_subch_open (struct inode *inode, struct file *file)
{
	int rc = 0;
	int size = 1;
	int len = 0;
	int i = 0;
	int j = 0;
	tempinfo_t *info;

	info = (tempinfo_t *) vmalloc (sizeof (tempinfo_t));
	if (info == NULL) {
		printk (KERN_WARNING "No memory available for data\n");
		return -ENOMEM;
	} else {
		file->private_data = (void *) info;
	}

	size += (highest_subchannel + 1) * 128;
	info->data = (char *) vmalloc (size);

	if (size && info->data == NULL) {
		printk (KERN_WARNING "No memory available for data\n");
		vfree (info);
		return -ENOMEM;
	}

	len += sprintf (info->data + len,
			"Device sch.  Dev Type/Model CU  in use  PIM PAM POM CHPIDs\n");
	len += sprintf (info->data + len,
			"---------------------------------------------------------------------\n");

	for (i = 0; i <= highest_subchannel; i++) {
		if (!((ioinfo[i] == NULL) || (ioinfo[i] == INVALID_STORAGE_AREA)
		      || (ioinfo[i]->st )|| !(ioinfo[i]->ui.flags.oper))) {
			len +=
			    sprintf (info->data + len, "%04X   %04X  ",
				     ioinfo[i]->schib.pmcw.dev, i);
			if (ioinfo[i]->senseid.dev_type != 0) {
				len += sprintf (info->data + len,
						"%04X/%02X   %04X/%02X",
						ioinfo[i]->senseid.dev_type,
						ioinfo[i]->senseid.dev_model,
						ioinfo[i]->senseid.cu_type,
						ioinfo[i]->senseid.cu_model);
			} else {
				len += sprintf (info->data + len,
						"          %04X/%02X",
						ioinfo[i]->senseid.cu_type,
						ioinfo[i]->senseid.cu_model);
			}
			if (ioinfo[i]->ui.flags.ready) {
				len += sprintf (info->data + len, "  yes ");
			} else {
				len += sprintf (info->data + len, "      ");
			}
			len += sprintf (info->data + len,
					"    %02X  %02X  %02X  ",
					ioinfo[i]->schib.pmcw.pim,
					ioinfo[i]->schib.pmcw.pam,
					ioinfo[i]->schib.pmcw.pom);
			for (j = 0; j < 8; j++) {
				len += sprintf (info->data + len,
						"%02X",
						ioinfo[i]->schib.pmcw.chpid[j]);
				if (j == 3) {
					len += sprintf (info->data + len, " ");
				}
			}
			len += sprintf (info->data + len, "\n");
		}
	}
	info->len = len;

	return rc;
}

static int
chan_subch_close (struct inode *inode, struct file *file)
{
	int rc = 0;
	tempinfo_t *p_info = (tempinfo_t *) file->private_data;

	if (p_info) {
		if (p_info->data)
			vfree (p_info->data);
		vfree (p_info);
	}

	return rc;
}

static ssize_t
chan_subch_read (struct file *file, char *user_buf, size_t user_len,
		 loff_t * offset)
{
	loff_t len;
	tempinfo_t *p_info = (tempinfo_t *) file->private_data;

	if (*offset >= p_info->len) {
		return 0;
	} else {
		len = min ((size_t) user_len, 
			   (size_t) (p_info->len - *offset));
		if (copy_to_user (user_buf, &(p_info->data[*offset]), len))
			return -EFAULT;
		(*offset) += len;
		return len;
	}
}

static struct file_operations chan_subch_file_ops = {
	read:chan_subch_read, open:chan_subch_open, release:chan_subch_close,
};

static int
chan_proc_init (void)
{
	chan_subch_entry =
	    create_proc_entry ("subchannels", S_IFREG | S_IRUGO, &proc_root);
	chan_subch_entry->proc_fops = &chan_subch_file_ops;

	return 1;
}

__initcall (chan_proc_init);

/*
 * Entry /proc/irq_count
 * display how many irqs have occured per cpu...
 */

static struct proc_dir_entry *cio_irq_proc_entry;

static int
cio_irq_proc_open (struct inode *inode, struct file *file)
{
	int rc = 0;
	int size = 1;
	int len = 0;
	tempinfo_t *info;
	int i;

	info = (tempinfo_t *) vmalloc (sizeof (tempinfo_t));
	if (info == NULL) {
		printk (KERN_WARNING "No memory available for data\n");
		rc = -ENOMEM;
	} else {
		file->private_data = (void *) info;
		size += NR_CPUS * 16;
		info->data = (char *) vmalloc (size);
		if (size && info->data == NULL) {
			printk (KERN_WARNING "No memory available for data\n");
			vfree (info);
			rc = -ENOMEM;
		} else {
			for (i = 0; i < NR_CPUS; i++) {
				if (s390_irq_count[i] != 0)
					len +=
					    sprintf (info->data + len, "%lx\n",
						     s390_irq_count[i]);
			}
			info->len = len;
		}
	}
	return rc;
}

static int
cio_irq_proc_close (struct inode *inode, struct file *file)
{
	int rc = 0;
	tempinfo_t *p_info = (tempinfo_t *) file->private_data;

	if (p_info) {
		if (p_info->data)
			vfree (p_info->data);
		vfree (p_info);
	}

	return rc;
}

static ssize_t
cio_irq_proc_read (struct file *file, char *user_buf, size_t user_len,
		   loff_t * offset)
{
	loff_t len;
	tempinfo_t *p_info = (tempinfo_t *) file->private_data;

	if (*offset >= p_info->len) {
		return 0;
	} else {
		len = min ((size_t) user_len, 
			   (size_t) (p_info->len - *offset));
		if (copy_to_user (user_buf, &(p_info->data[*offset]), len))
			return -EFAULT;
		(*offset) += len;
		return len;
	}
}

static struct file_operations cio_irq_proc_file_ops = {
	read:cio_irq_proc_read, open:cio_irq_proc_open,
	release:cio_irq_proc_close,
};

static int
cio_irq_proc_init (void)
{
	int i;

	if (cio_count_irqs) {
		for (i = 0; i < NR_CPUS; i++)
			s390_irq_count[i] = 0;
		cio_irq_proc_entry =
		    create_proc_entry ("irq_count", S_IFREG | S_IRUGO,
				       &proc_root);
		cio_irq_proc_entry->proc_fops = &cio_irq_proc_file_ops;
	}

	return 1;
}

__initcall (cio_irq_proc_init);

void
init_irq_proc(void)
{
	/* For now, nothing... */
}
