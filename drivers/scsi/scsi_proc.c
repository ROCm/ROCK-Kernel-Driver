/*
 * linux/drivers/scsi/scsi_proc.c
 *
 * The functions in this file provide an interface between
 * the PROC file system and the SCSI device drivers
 * It is mainly used for debugging, statistics and to pass 
 * information directly to the lowlevel driver.
 *
 * (c) 1995 Michael Neuffer neuffer@goofy.zdv.uni-mainz.de 
 * Version: 0.99.8   last change: 95/09/13
 * 
 * generic command parser provided by: 
 * Andreas Heilwagen <crashcar@informatik.uni-koblenz.de>
 *
 * generic_proc_info() support of xxxx_info() by:
 * Michael A. Griffith <grif@acm.org>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/blk.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>

#include "scsi.h"
#include "hosts.h"

#include "scsi_priv.h"
#include "scsi_logging.h"


/* 4K page size, but our output routines, use some slack for overruns */
#define PROC_BLOCK_SIZE (3*1024)

/* XXX: this shouldn't really be exposed to drivers. */
struct proc_dir_entry *proc_scsi;
EXPORT_SYMBOL(proc_scsi);


static int proc_scsi_read(char *buffer, char **start, off_t offset,
			  int length, int *eof, void *data)
{
	struct Scsi_Host *shost = data;
	int n;

	n = shost->hostt->proc_info(shost, buffer, start, offset, length, 0);
	*eof = (n < length);

	return n;
}

static int proc_scsi_write_proc(struct file *file, const char *buf,
                           unsigned long count, void *data)
{
	struct Scsi_Host *shost = data;
	ssize_t ret = -ENOMEM;
	char *page;
	char *start;
    
	if (count > PROC_BLOCK_SIZE)
		return -EOVERFLOW;

	page = (char *)__get_free_page(GFP_KERNEL);
	if (page) {
		ret = -EFAULT;
		if (copy_from_user(page, buf, count))
			goto out;
		ret = shost->hostt->proc_info(shost, page, &start, 0, count, 1);
	}
out:
	free_page((unsigned long)page);
	return ret;
}

void scsi_proc_host_add(struct Scsi_Host *shost)
{
	struct scsi_host_template *sht = shost->hostt;
	struct proc_dir_entry *p;
	char name[10];

	if (!sht->proc_info)
		return;

	if (!sht->proc_dir) {
		sht->proc_dir = proc_mkdir(sht->proc_name, proc_scsi);
        	if (!sht->proc_dir) {
			printk(KERN_ERR "%s: proc_mkdir failed for %s\n",
			       __FUNCTION__, sht->proc_name);
			return;
		}
		sht->proc_dir->owner = sht->module;
	}

	sprintf(name,"%d", shost->host_no);
	p = create_proc_read_entry(name, S_IFREG | S_IRUGO | S_IWUSR,
			sht->proc_dir, proc_scsi_read, shost);
	if (!p) {
		printk(KERN_ERR "%s: Failed to register host %d in"
		       "%s\n", __FUNCTION__, shost->host_no,
		       sht->proc_name);
		return;
	} 

	p->write_proc = proc_scsi_write_proc;
	p->owner = shost->hostt->module;
}

void scsi_proc_host_rm(struct Scsi_Host *shost)
{
	struct scsi_host_template *sht = shost->hostt;
	char name[10];

	if (sht->proc_info) {
		sprintf(name,"%d", shost->host_no);
		remove_proc_entry(name, sht->proc_dir);
		if (!sht->present)
			remove_proc_entry(sht->proc_name, proc_scsi);
	}
}

static int proc_print_scsidevice(struct device *dev, void *data)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct seq_file *s = data;
	int i;

	seq_printf(s,
		"Host: scsi%d Channel: %02d Id: %02d Lun: %02d\n  Vendor: ",
		sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);
	for (i = 0; i < 8; i++) {
		if (sdev->vendor[i] >= 0x20)
			seq_printf(s, "%c", sdev->vendor[i]);
		else
			seq_printf(s, " ");
	}

	seq_printf(s, " Model: ");
	for (i = 0; i < 16; i++) {
		if (sdev->model[i] >= 0x20)
			seq_printf(s, "%c", sdev->model[i]);
		else
			seq_printf(s, " ");
	}

	seq_printf(s, " Rev: ");
	for (i = 0; i < 4; i++) {
		if (sdev->rev[i] >= 0x20)
			seq_printf(s, "%c", sdev->rev[i]);
		else
			seq_printf(s, " ");
	}

	seq_printf(s, "\n");

	seq_printf(s, "  Type:   %s ",
		     sdev->type < MAX_SCSI_DEVICE_CODE ?
	       scsi_device_types[(int) sdev->type] : "Unknown          ");
	seq_printf(s, "               ANSI"
		     " SCSI revision: %02x", (sdev->scsi_level - 1) ?
		     sdev->scsi_level - 1 : 1);
	if (sdev->scsi_level == 2)
		seq_printf(s, " CCS\n");
	else
		seq_printf(s, "\n");

	return 0;
}

static int scsi_add_single_device(uint host, uint channel, uint id, uint lun)
{
	struct Scsi_Host *shost;
	struct scsi_device *sdev;
	int error = -ENODEV;

	shost = scsi_host_lookup(host);
	if (!shost)
		return -ENODEV;

	if (!scsi_find_device(shost, channel, id, lun)) {
		sdev = scsi_add_device(shost, channel, id, lun);
		if (IS_ERR(sdev))
			error = PTR_ERR(sdev);
		else
			error = 0;
	}

	scsi_host_put(shost);
	return error;
}

static int scsi_remove_single_device(uint host, uint channel, uint id, uint lun)
{
	struct scsi_device *sdev;
	struct Scsi_Host *shost;
	int error = -ENODEV;

	shost = scsi_host_lookup(host);
	if (!shost)
		return -ENODEV;
	sdev = scsi_find_device(shost, channel, id, lun);
	if (!sdev)
		goto out;
	if (sdev->access_count)
		goto out;

	error = scsi_remove_device(sdev);
out:
	scsi_host_put(shost);
	return error;
}

static int proc_scsi_write(struct file *file, const char* buf,
                           size_t length, loff_t *ppos)
{
	int host, channel, id, lun;
	char *buffer, *p;
	int err;

	if (!buf || length>PAGE_SIZE)
		return -EINVAL;

	buffer = (char *)__get_free_page(GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	if (copy_from_user(buffer, buf, length)) {
		err =-EFAULT;
		goto out;
	}

	err = -EINVAL;

	if (length < PAGE_SIZE)
		buffer[length] = '\0';
	else if (buffer[PAGE_SIZE-1])
		goto out;

	if (length < 11 || strncmp("scsi", buffer, 4))
		goto out;

#ifdef CONFIG_SCSI_LOGGING
	/*
	 * Usage: echo "scsi log token #N" > /proc/scsi/scsi
	 * where token is one of [error,scan,mlqueue,mlcomplete,llqueue,
	 * llcomplete,hlqueue,hlcomplete]
	 */
	if (!strncmp("log", buffer + 5, 3)) {
		char *token;
		unsigned int level;

		p = buffer + 9;
		token = p;
		while (*p != ' ' && *p != '\t' && *p != '\0') {
			p++;
		}

		if (*p == '\0') {
			if (strncmp(token, "all", 3) == 0) {
				/*
				 * Turn on absolutely everything.
				 */
				scsi_logging_level = ~0;
			} else if (strncmp(token, "none", 4) == 0) {
				/*
				 * Turn off absolutely everything.
				 */
				scsi_logging_level = 0;
			} else {
				goto out;
			}
		} else {
			*p++ = '\0';

			level = simple_strtoul(p, NULL, 0);

			/*
			 * Now figure out what to do with it.
			 */
			if (strcmp(token, "error") == 0) {
				SCSI_SET_ERROR_RECOVERY_LOGGING(level);
			} else if (strcmp(token, "timeout") == 0) {
				SCSI_SET_TIMEOUT_LOGGING(level);
			} else if (strcmp(token, "scan") == 0) {
				SCSI_SET_SCAN_BUS_LOGGING(level);
			} else if (strcmp(token, "mlqueue") == 0) {
				SCSI_SET_MLQUEUE_LOGGING(level);
			} else if (strcmp(token, "mlcomplete") == 0) {
				SCSI_SET_MLCOMPLETE_LOGGING(level);
			} else if (strcmp(token, "llqueue") == 0) {
				SCSI_SET_LLQUEUE_LOGGING(level);
			} else if (strcmp(token, "llcomplete") == 0) {
				SCSI_SET_LLCOMPLETE_LOGGING(level);
			} else if (strcmp(token, "hlqueue") == 0) {
				SCSI_SET_HLQUEUE_LOGGING(level);
			} else if (strcmp(token, "hlcomplete") == 0) {
				SCSI_SET_HLCOMPLETE_LOGGING(level);
			} else if (strcmp(token, "ioctl") == 0) {
				SCSI_SET_IOCTL_LOGGING(level);
			} else {
				goto out;
			}
		}

		printk(KERN_INFO "scsi logging level set to 0x%8.8x\n", scsi_logging_level);
	}
#endif	/* CONFIG_SCSI_LOGGING */

	/*
	 * Usage: echo "scsi add-single-device 0 1 2 3" >/proc/scsi/scsi
	 * with  "0 1 2 3" replaced by your "Host Channel Id Lun".
	 * Consider this feature BETA.
	 *     CAUTION: This is not for hotplugging your peripherals. As
	 *     SCSI was not designed for this you could damage your
	 *     hardware !
	 * However perhaps it is legal to switch on an
	 * already connected device. It is perhaps not
	 * guaranteed this device doesn't corrupt an ongoing data transfer.
	 */
	if (!strncmp("add-single-device", buffer + 5, 17)) {
		p = buffer + 23;

		host = simple_strtoul(p, &p, 0);
		channel = simple_strtoul(p + 1, &p, 0);
		id = simple_strtoul(p + 1, &p, 0);
		lun = simple_strtoul(p + 1, &p, 0);

		err = scsi_add_single_device(host, channel, id, lun);
		if (err >= 0)
			err = length;
	/*
	 * Usage: echo "scsi remove-single-device 0 1 2 3" >/proc/scsi/scsi
	 * with  "0 1 2 3" replaced by your "Host Channel Id Lun".
	 *
	 * Consider this feature pre-BETA.
	 *
	 *     CAUTION: This is not for hotplugging your peripherals. As
	 *     SCSI was not designed for this you could damage your
	 *     hardware and thoroughly confuse the SCSI subsystem.
	 *
	 */
	} else if (!strncmp("remove-single-device", buffer + 5, 20)) {
		p = buffer + 26;

		host = simple_strtoul(p, &p, 0);
		channel = simple_strtoul(p + 1, &p, 0);
		id = simple_strtoul(p + 1, &p, 0);
		lun = simple_strtoul(p + 1, &p, 0);

		err = scsi_remove_single_device(host, channel, id, lun);
	}
out:
	
	free_page((unsigned long)buffer);
	return err;
}

static int proc_scsi_show(struct seq_file *s, void *p)
{
	seq_printf(s, "Attached devices:\n");
	bus_for_each_dev(&scsi_bus_type, NULL, s, proc_print_scsidevice);
	return 0;
}

static int proc_scsi_open(struct inode *inode, struct file *file)
{
	/*
	 * We don't really needs this for the write case but it doesn't
	 * harm either.
	 */
	return single_open(file, proc_scsi_show, NULL);
}

static struct file_operations proc_scsi_operations = {
	.open		= proc_scsi_open,
	.read		= seq_read,
	.write		= proc_scsi_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int __init scsi_init_procfs(void)
{
	struct proc_dir_entry *pde;

	proc_scsi = proc_mkdir("scsi", 0);
	if (!proc_scsi)
		goto err1;

	pde = create_proc_entry("scsi/scsi", 0, NULL);
	if (!pde)
		goto err2;
	pde->proc_fops = &proc_scsi_operations;

	return 0;

err2:
	remove_proc_entry("scsi", 0);
err1:
	return -ENOMEM;
}

void scsi_exit_procfs(void)
{
	remove_proc_entry("scsi/scsi", 0);
	remove_proc_entry("scsi", 0);
}
