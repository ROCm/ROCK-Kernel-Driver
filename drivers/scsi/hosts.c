/*
 *  hosts.c Copyright (C) 1992 Drew Eckhardt
 *          Copyright (C) 1993, 1994, 1995 Eric Youngdale
 *
 *  mid to lowlevel SCSI driver interface
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *  Jiffies wrap fixes (host->resetting), 3 Dec 1998 Andrea Arcangeli
 *  Added QLOGIC QLA1280 SCSI controller kernel host support. 
 *     August 4, 1999 Fred Lewis, Intel DuPont
 *
 *  Updated to reflect the new initialization scheme for the higher 
 *  level of scsi drivers (sd/sr/st)
 *  September 17, 2000 Torben Mathiasen <tmm@image.dk>
 *
 *  Restructured scsi_host lists and associated functions.
 *  September 04, 2002 Mike Anderson (andmike@us.ibm.com)
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/completion.h>
#include <linux/unistd.h>

#include "scsi.h"
#include "hosts.h"

#include "scsi_priv.h"
#include "scsi_logging.h"


static int scsi_host_next_hn;		/* host_no for next new host */

/**
 * scsi_remove_host - check a scsi host for release and release
 * @shost:	a pointer to a scsi host to release
 *
 * Return value:
 * 	0 on Success / 1 on Failure
 **/
int scsi_remove_host(struct Scsi_Host *shost)
{
	struct scsi_device *sdev;

	/*
	 * FIXME Do ref counting.  We force all of the devices offline to
	 * help prevent race conditions where other hosts/processors could
	 * try and get in and queue a command.
	 */
	list_for_each_entry(sdev, &shost->my_devices, siblings)
		sdev->online = FALSE;

	scsi_proc_host_rm(shost);
	scsi_forget_host(shost);
	scsi_sysfs_remove_host(shost);

	return 0;
}

/**
 * scsi_add_host - add a scsi host
 * @shost:	scsi host pointer to add
 * @dev:	a struct device of type scsi class
 *
 * Return value: 
 * 	0 on success / != 0 for error
 **/
int scsi_add_host(struct Scsi_Host *shost, struct device *dev)
{
	Scsi_Host_Template *sht = shost->hostt;
	int error;

	printk(KERN_INFO "scsi%d : %s\n", shost->host_no,
			sht->info ? sht->info(shost) : sht->name);

	error = scsi_sysfs_add_host(shost, dev);
	if (!error) {
		scsi_proc_host_add(shost);
		scsi_scan_host(shost);
	}
			
	return error;
}

/**
 * scsi_free_sdev - free a scsi hosts resources
 * @shost:	scsi host to free 
 **/
void scsi_free_shost(struct Scsi_Host *shost)
{
	if (shost->ehandler) {
		DECLARE_COMPLETION(sem);
		shost->eh_notify = &sem;
		shost->eh_kill = 1;
		up(shost->eh_wait);
		wait_for_completion(&sem);
		shost->eh_notify = NULL;
	}

	shost->hostt->present--;
	scsi_destroy_command_freelist(shost);
	kfree(shost);
}

/**
 * scsi_host_alloc - register a scsi host adapter instance.
 * @sht:	pointer to scsi host template
 * @privsize:	extra bytes to allocate for driver
 *
 * Note:
 * 	Allocate a new Scsi_Host and perform basic initialization.
 * 	The host is not published to the scsi midlayer until scsi_add_host
 * 	is called.
 *
 * Return value:
 * 	Pointer to a new Scsi_Host
 **/
struct Scsi_Host *scsi_host_alloc(Scsi_Host_Template *sht, int privsize)
{
	struct Scsi_Host *shost;
	int gfp_mask = GFP_KERNEL, rval;
	DECLARE_COMPLETION(complete);

	if (sht->unchecked_isa_dma && privsize)
		gfp_mask |= __GFP_DMA;

        /* Check to see if this host has any error handling facilities */
        if (!sht->eh_strategy_handler && !sht->eh_abort_handler &&
	    !sht->eh_device_reset_handler && !sht->eh_bus_reset_handler &&
            !sht->eh_host_reset_handler) {
		printk(KERN_ERR "ERROR: SCSI host `%s' has no error handling\n"
				"ERROR: This is not a safe way to run your "
				        "SCSI host\n"
				"ERROR: The error handling must be added to "
				"this driver\n", sht->proc_name);
		dump_stack();
        }

	/* if its not set in the template, use the default */
	if (!sht->shost_attrs)
		 sht->shost_attrs = scsi_sysfs_shost_attrs;
	if (!sht->sdev_attrs)
		 sht->sdev_attrs = scsi_sysfs_sdev_attrs;

	shost = kmalloc(sizeof(struct Scsi_Host) + privsize, gfp_mask);
	if (!shost)
		return NULL;
	memset(shost, 0, sizeof(struct Scsi_Host) + privsize);

	spin_lock_init(&shost->default_lock);
	scsi_assign_lock(shost, &shost->default_lock);
	INIT_LIST_HEAD(&shost->my_devices);
	INIT_LIST_HEAD(&shost->eh_cmd_q);
	INIT_LIST_HEAD(&shost->starved_list);
	init_waitqueue_head(&shost->host_wait);

	shost->host_no = scsi_host_next_hn++; /* XXX(hch): still racy */
	shost->dma_channel = 0xff;

	/* These three are default values which can be overridden */
	shost->max_channel = 0;
	shost->max_id = 8;
	shost->max_lun = 8;

	/*
	 * All drivers right now should be able to handle 12 byte
	 * commands.  Every so often there are requests for 16 byte
	 * commands, but individual low-level drivers need to certify that
	 * they actually do something sensible with such commands.
	 */
	shost->max_cmd_len = 12;
	shost->hostt = sht;
	shost->this_id = sht->this_id;
	shost->can_queue = sht->can_queue;
	shost->sg_tablesize = sht->sg_tablesize;
	shost->cmd_per_lun = sht->cmd_per_lun;
	shost->unchecked_isa_dma = sht->unchecked_isa_dma;
	shost->use_clustering = sht->use_clustering;
	shost->use_blk_tcq = sht->use_blk_tcq;
	shost->highmem_io = sht->highmem_io;

	if (!sht->max_host_blocked)
		shost->max_host_blocked = sht->max_host_blocked;
	else
		shost->max_host_blocked = SCSI_DEFAULT_HOST_BLOCKED;

	/*
	 * If the driver imposes no hard sector transfer limit, start at
	 * machine infinity initially.
	 */
	if (sht->max_sectors)
		shost->max_sectors = sht->max_sectors;
	else
		shost->max_sectors = SCSI_DEFAULT_MAX_SECTORS;

	rval = scsi_setup_command_freelist(shost);
	if (rval)
		goto fail;

	scsi_sysfs_init_host(shost);

	shost->eh_notify = &complete;
	/* XXX(hch): handle error return */
	kernel_thread((int (*)(void *))scsi_error_handler, shost, 0);
	wait_for_completion(&complete);
	shost->eh_notify = NULL;
	shost->hostt->present++;
	return shost;
 fail:
	kfree(shost);
	return NULL;
}

struct Scsi_Host *scsi_register(Scsi_Host_Template *sht, int privsize)
{
	struct Scsi_Host *shost = scsi_host_alloc(sht, privsize);

	if (!sht->detect) {
		printk(KERN_WARNING "scsi_register() called on new-style "
				    "template for driver %s\n", sht->name);
		dump_stack();
	}

	if (shost)
		list_add_tail(&shost->sht_legacy_list, &sht->legacy_hosts);
	return shost;
}

void scsi_unregister(struct Scsi_Host *shost)
{
	list_del(&shost->sht_legacy_list);
	scsi_host_put(shost);
}

/**
 * scsi_host_lookup - get a reference to a Scsi_Host by host no
 *
 * @hostnum:	host number to locate
 *
 * Return value:
 *	A pointer to located Scsi_Host or NULL.
 **/
struct Scsi_Host *scsi_host_lookup(unsigned short hostnum)
{
	struct class *class = class_get(&shost_class);
	struct class_device *cdev;
	struct Scsi_Host *shost = NULL, *p;

	if (class) {
		down_read(&class->subsys.rwsem);
		list_for_each_entry(cdev, &class->children, node) {
			p = class_to_shost(cdev);
			if (p->host_no == hostnum) {
				scsi_host_get(p);
				shost = p;
				break;
			}
		}
		up_read(&class->subsys.rwsem);
	}

	return shost;
}

/**
 * *scsi_host_get - inc a Scsi_Host ref count
 * @shost:	Pointer to Scsi_Host to inc.
 **/
void scsi_host_get(struct Scsi_Host *shost)
{
	get_device(&shost->host_gendev);
	class_device_get(&shost->class_dev);
}

/**
 * *scsi_host_put - dec a Scsi_Host ref count
 * @shost:	Pointer to Scsi_Host to dec.
 **/
void scsi_host_put(struct Scsi_Host *shost)
{
	class_device_put(&shost->class_dev);
	put_device(&shost->host_gendev);
}

void scsi_host_busy_inc(struct Scsi_Host *shost, Scsi_Device *sdev)
{
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	shost->host_busy++;
	sdev->device_busy++;
	spin_unlock_irqrestore(shost->host_lock, flags);
}

void scsi_host_busy_dec_and_test(struct Scsi_Host *shost, Scsi_Device *sdev)
{
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	shost->host_busy--;
	if (shost->in_recovery && shost->host_failed &&
	    (shost->host_busy == shost->host_failed))
	{
		up(shost->eh_wait);
		SCSI_LOG_ERROR_RECOVERY(5, printk("Waking error handler"
					  " thread\n"));
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
}
