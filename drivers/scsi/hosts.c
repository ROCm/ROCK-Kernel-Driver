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


/*
 *  This file contains the medium level SCSI
 *  host interface initialization, as well as the scsi_hosts list of SCSI
 *  hosts currently present in the system.
 */

#include <linux/module.h>
#include <linux/blk.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/smp_lock.h>

#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>
#include <asm/dma.h>

#include "scsi.h"
#include "hosts.h"

LIST_HEAD(scsi_host_tmpl_list);
LIST_HEAD(scsi_host_hn_list);

LIST_HEAD(scsi_host_list);
spinlock_t scsi_host_list_lock = SPIN_LOCK_UNLOCKED;

struct Scsi_Device_Template * scsi_devicelist;

static int scsi_host_next_hn;		/* host_no for next new host */
static int scsi_hosts_registered;	/* cnt of registered scsi hosts */

/**
 * scsi_tp_for_each_host - call function for each scsi host off a template
 * @shost_tp:	a pointer to a scsi host template
 * @callback:	a pointer to callback function
 *
 * Return value:
 * 	0 on Success / 1 on Failure
 **/
int scsi_tp_for_each_host(Scsi_Host_Template *shost_tp, int
			    (*callback)(struct Scsi_Host *shost))
{
	struct list_head *lh, *lh_sf;
	struct Scsi_Host *shost;

	spin_lock(&scsi_host_list_lock);

	list_for_each_safe(lh, lh_sf, &scsi_host_list) {
		shost = list_entry(lh, struct Scsi_Host, sh_list);
		if (shost->hostt == shost_tp) {
			spin_unlock(&scsi_host_list_lock);
			callback(shost);
			spin_lock(&scsi_host_list_lock);
		}
	}

	spin_unlock(&scsi_host_list_lock);

	return 0;
}

/**
 * scsi_host_generic_release - default release function for hosts
 * @shost: 
 * 
 * Description:
 * 	This is the default case for the release function.  Its completely
 *	useless for anything but old ISA adapters
 **/
static void scsi_host_generic_release(struct Scsi_Host *shost)
{
	if (shost->irq)
		free_irq(shost->irq, NULL);
	if (shost->dma_channel != 0xff)
		free_dma(shost->dma_channel);
	if (shost->io_port && shost->n_io_port)
		release_region(shost->io_port, shost->n_io_port);
}

/**
 * scsi_host_chk_and_release - check a scsi host for release and release
 * @shost:	a pointer to a scsi host to release
 *
 * Return value:
 * 	0 on Success / 1 on Failure
 **/
int scsi_host_chk_and_release(struct Scsi_Host *shost)
{
	int pcount;
	Scsi_Device *sdev;
	struct Scsi_Device_Template *sdev_tp;
	Scsi_Cmnd *scmd;

	/*
	 * Current policy is all shosts go away on unregister.
	 */
	if (shost->hostt->module && GET_USE_COUNT(shost->hostt->module))
		return 1;

	/*
	 * FIXME Do ref counting.  We force all of the devices offline to
	 * help prevent race conditions where other hosts/processors could
	 * try and get in and queue a command.
	 */
	for (sdev = shost->host_queue; sdev; sdev = sdev->next) 
		sdev->online = FALSE;

	for (sdev = shost->host_queue; sdev; sdev = sdev->next) {
		/*
		 * Loop over all of the commands associated with the
		 * device.  If any of them are busy, then set the state
		 * back to inactive and bail.
		 */
		for (scmd = sdev->device_queue; scmd; scmd = scmd->next) {
			if (scmd->request && scmd->request->rq_status !=
			    RQ_INACTIVE) {
				printk(KERN_ERR "SCSI device not inactive"
				       "- rq_status=%d, target=%d, pid=%ld,"
				       "state=%d, owner=%d.\n",
				       scmd->request->rq_status,
				       scmd->target, scmd->pid,
				       scmd->state, scmd->owner);
				for (sdev = shost->host_queue; sdev;
				     sdev = sdev->next) {
					for (scmd = sdev->device_queue; scmd;
					     scmd = scmd->next)
						if (scmd->request->rq_status ==
						    RQ_SCSI_DISCONNECTING)
							scmd->request->rq_status = RQ_INACTIVE;
				}
				printk(KERN_ERR "Device busy???\n");
				return 1;
			}
			/*
			 * No, this device is really free.  Mark it as such, and
			 * continue on.
			 */
			scmd->state = SCSI_STATE_DISCONNECTING;
			if (scmd->request)
				scmd->request->rq_status =
					RQ_SCSI_DISCONNECTING;	/* Mark as
								   busy */
		}
	}

	/*
	 * Next we detach the high level drivers from the Scsi_Device
	 * structures
	 */
	for (sdev = shost->host_queue; sdev; sdev = sdev->next) {
		for (sdev_tp = scsi_devicelist; sdev_tp;
		     sdev_tp = sdev_tp->next)
			if (sdev_tp->detach)
				(*sdev_tp->detach) (sdev);

		/* If something still attached, punt */
		if (sdev->attached) {
			printk(KERN_ERR "Attached usage count = %d\n",
			       sdev->attached);
			return 1;
		}

		if (shost->hostt->slave_detach)
			(*shost->hostt->slave_detach) (sdev);

		devfs_unregister(sdev->de);
		device_unregister(&sdev->sdev_driverfs_dev);
	}

	/* Next we free up the Scsi_Cmnd structures for this host */

	for (sdev = shost->host_queue; sdev;
	     sdev = shost->host_queue) {
		scsi_release_commandblocks(sdev);
		blk_cleanup_queue(&sdev->request_queue);
		/* Next free up the Scsi_Device structures for this host */
		shost->host_queue = sdev->next;
		if (sdev->inquiry)
			kfree(sdev->inquiry);
		kfree(sdev);
	}

	/* Remove the instance of the individual hosts */
	pcount = scsi_hosts_registered;
	if (shost->hostt->release)
		(*shost->hostt->release) (shost);
	else {
		scsi_host_generic_release(shost);
	}

	if (pcount == scsi_hosts_registered)
		scsi_unregister(shost);

	return 0;
}

/**
 * scsi_unregister - unregister a scsi host
 * @shost:	scsi host to be unregistered
 **/
void scsi_unregister(struct Scsi_Host *shost)
{
	struct list_head *lh;
	Scsi_Host_Name *shost_name;

	/* Remove shost from scsi_host_list */
	spin_lock(&scsi_host_list_lock);
	list_del(&shost->sh_list);
	spin_unlock(&scsi_host_list_lock);

	/* Unregister from scsi_host_hn_list */
	list_for_each(lh, &scsi_host_hn_list) {
		shost_name = list_entry(lh, Scsi_Host_Name, shn_list);
		if (shost->host_no == shost_name->host_no)
			shost_name->host_registered = 0;
	}

	/*
	 * Next, kill the kernel error recovery thread for this host.
	 */
	if (shost->ehandler) {
		DECLARE_MUTEX_LOCKED(sem);
		shost->eh_notify = &sem;
		send_sig(SIGHUP, shost->ehandler, 1);
		down(&sem);
		shost->eh_notify = NULL;
	}

	scsi_hosts_registered--;
	shost->hostt->present--;

	/* Cleanup proc and driverfs */
#ifdef CONFIG_PROC_FS
	scsi_proc_host_rm(shost);
	if (!shost->hostt->present)
		remove_proc_entry(shost->hostt->proc_name, proc_scsi);
#endif
	device_unregister(&shost->host_driverfs_dev);

	kfree(shost);
}

/**
 * scsi_host_hn_add - allocate and add new Scsi_Host_Name
 * @name:	String to store in name field
 *
 * Return value:
 * 	Pointer to a new Scsi_Host_Name
 **/
Scsi_Host_Name *scsi_host_hn_add(char *name)
{
	Scsi_Host_Name *shost_name;
	int len;

	len = strlen(name);
	shost_name =  kmalloc(sizeof(*shost_name), GFP_KERNEL);
	if (!shost_name) {
		printk(KERN_ERR "%s: out of memory at line %d.\n",
		       __FUNCTION__, __LINE__);
		return NULL;
	}
	shost_name->name = kmalloc(len + 1, GFP_KERNEL);
	if (!shost_name->name) {
		kfree(shost_name);
		printk(KERN_ERR "%s: out of memory at line %d.\n",
		       __FUNCTION__, __LINE__);
		return NULL;
	}

	if (len)
		strncpy(shost_name->name, name, len);
	shost_name->name[len] = 0;
	shost_name->host_no = scsi_host_next_hn++;
	shost_name->host_registered = 0;
	list_add_tail(&shost_name->shn_list, &scsi_host_hn_list);

	return shost_name;
}

/**
 * scsi_register - register a scsi host adapter instance.
 * @shost_tp:	pointer to scsi host template
 * @xtr_bytes:	extra bytes to allocate for driver
 *
 * Note:
 * 	We call this when we come across a new host adapter. We only do
 * 	this once we are 100% sure that we want to use this host adapter -
 * 	it is a pain to reverse this, so we try to avoid it 
 *
 * Return value:
 * 	Pointer to a new Scsi_Host
 **/
extern int blk_nohighio;
struct Scsi_Host * scsi_register(Scsi_Host_Template *shost_tp, int xtr_bytes)
{
	struct Scsi_Host *shost, *shost_scr;
	Scsi_Host_Name *shost_name = NULL;
	Scsi_Host_Name *shn = NULL;
	char *hname;
	size_t hname_len;
	struct list_head *lh;
	int gfp_mask;
	DECLARE_MUTEX_LOCKED(sem);

	gfp_mask = GFP_KERNEL;
	if (shost_tp->unchecked_isa_dma && xtr_bytes)
		gfp_mask |= __GFP_DMA;

	shost = kmalloc(sizeof(struct Scsi_Host) + xtr_bytes, gfp_mask);
	if (!shost) {
		printk(KERN_ERR "%s: out of memory.\n", __FUNCTION__);
		return NULL;
	}

	memset(shost, 0, sizeof(struct Scsi_Host) + xtr_bytes);

	/*
	 * Determine host number. Check reserved first before allocating
	 * new one
	 */
	hname = (shost_tp->proc_name) ?  shost_tp->proc_name : "";
	hname_len = strlen(hname);

	if (hname_len)
		list_for_each(lh, &scsi_host_hn_list) {
			shn = list_entry(lh, Scsi_Host_Name, shn_list);
			if (!(shn->host_registered) &&
			    !strncmp(hname, shn->name, hname_len)) {
				shost_name = shn;
				break;
			}
		}

	if (!shost_name) {
		shost_name = scsi_host_hn_add(hname);
		if (!shost_name) {
			kfree(shost);
			return NULL;
		}
	}

	shost->host_no = shost_name->host_no;
	shost_name->host_registered = 1;
	scsi_hosts_registered++;

	spin_lock_init(&shost->default_lock);
	scsi_assign_lock(shost, &shost->default_lock);
	atomic_set(&shost->host_active,0);

	init_waitqueue_head(&shost->host_wait);
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
	shost->hostt = shost_tp;
	shost->host_blocked = FALSE;
	shost->host_self_blocked = FALSE;

#ifdef DEBUG
	printk("%s: %x %x: %d\n", __FUNCTION_ (int)shost,
	       (int)shost->hostt, xtr_bytes);
#endif

	/*
	 * The next six are the default values which can be overridden if
	 * need be
	 */
	shost->this_id = shost_tp->this_id;
	shost->can_queue = shost_tp->can_queue;
	shost->sg_tablesize = shost_tp->sg_tablesize;
	shost->cmd_per_lun = shost_tp->cmd_per_lun;
	shost->unchecked_isa_dma = shost_tp->unchecked_isa_dma;
	shost->use_clustering = shost_tp->use_clustering;
	if (!blk_nohighio)
	shost->highmem_io = shost_tp->highmem_io;

	shost->max_sectors = shost_tp->max_sectors;
	shost->use_blk_tcq = shost_tp->use_blk_tcq;

	spin_lock(&scsi_host_list_lock);
	/*
	 * FIXME When device naming is complete remove this step that
	 * orders the scsi_host_list by host number and just do a
	 * list_add_tail.
	 */
	list_for_each(lh, &scsi_host_list) {
		shost_scr = list_entry(lh, struct Scsi_Host, sh_list);
		if (shost->host_no < shost_scr->host_no) {
			__list_add(&shost->sh_list, shost_scr->sh_list.prev,
				   &shost_scr->sh_list);
			goto found;
		}
	}
	list_add_tail(&shost->sh_list, &scsi_host_list);
found:
	spin_unlock(&scsi_host_list_lock);

#ifdef CONFIG_PROC_FS
	/* Add the new driver to /proc/scsi if not already there */
	if (!shost_tp->proc_dir)
		scsi_proc_host_mkdir(shost_tp);
	scsi_proc_host_add(shost);
#endif

	strncpy(shost->host_driverfs_dev.name, shost_tp->proc_name,
		DEVICE_NAME_SIZE-1);
	sprintf(shost->host_driverfs_dev.bus_id, "scsi%d",
		shost->host_no);

	shost->eh_notify = &sem;
	kernel_thread((int (*)(void *)) scsi_error_handler, (void *) shost, 0);
	/*
	 * Now wait for the kernel error thread to initialize itself
	 * as it might be needed when we scan the bus.
	 */
	down(&sem);
	shost->eh_notify = NULL;

	shost->hostt->present++;

	return shost;
}


/**
 * scsi_register_host - register a low level host driver
 * @shost_tp:	pointer to a scsi host driver template
 *
 * Return value:
 * 	0 on Success / 1 on Failure.
 **/
int scsi_register_host(Scsi_Host_Template *shost_tp)
{
	int cur_cnt;
	Scsi_Device *sdev;
	struct Scsi_Device_Template *sdev_tp;
	struct list_head *lh;
	struct Scsi_Host *shost;

	INIT_LIST_HEAD(&shost_tp->shtp_list);

	/*
	 * Check no detect routine.
	 */
	if (!shost_tp->detect)
		return 1;

	/* If max_sectors isn't set, default to max */
	if (!shost_tp->max_sectors)
		shost_tp->max_sectors = 1024;

	cur_cnt = scsi_hosts_registered;

	MOD_INC_USE_COUNT;

	/*
	 * The detect routine must carefully spinunlock/spinlock if it
	 * enables interrupts, since all interrupt handlers do spinlock as
	 * well.
	 */

	/*
	 * detect should do its own locking
	 * FIXME present is now set is scsi_register which breaks manual
	 * registration code below.
	 */
	shost_tp->detect(shost_tp);

	if (shost_tp->present) {
			/*
			 * FIXME Who needs manual registration and why???
			 */
		if (cur_cnt == scsi_hosts_registered) {
			if (shost_tp->present > 1) {
				printk(KERN_ERR "scsi: Failure to register"
				       "low-level scsi driver");
				scsi_unregister_host(shost_tp);
				return 1;
			}
			/*
			 * The low-level driver failed to register a driver.
			 * We can do this now.
			 */
			if(scsi_register(shost_tp, 0)==NULL) {
				printk(KERN_ERR "scsi: register failed.\n");
				scsi_unregister_host(shost_tp);
				return 1;
			}
		}

		list_add_tail(&shost_tp->shtp_list, &scsi_host_tmpl_list);

		/* The next step is to call scan_scsis here.  This generates the
		 * Scsi_Devices entries
		 */
		list_for_each(lh, &scsi_host_list) {
			shost = list_entry(lh, struct Scsi_Host, sh_list);
			if (shost->hostt == shost_tp) {
				const char *dm_name;
				if (shost_tp->info) {
					dm_name = shost_tp->info(shost);
				} else {
					dm_name = shost_tp->name;
				}
				printk(KERN_INFO "scsi%d : %s\n",
				       shost->host_no, dm_name);

				/* first register parent with driverfs */
				device_register(&shost->host_driverfs_dev);
				scan_scsis(shost, 0, 0, 0, 0);
			}
		}

		for (sdev_tp = scsi_devicelist; sdev_tp;
		     sdev_tp = sdev_tp->next) {
			if (sdev_tp->init && sdev_tp->dev_noticed)
				(*sdev_tp->init) ();
		}

		/*
		 * Next we create the Scsi_Cmnd structures for this host 
		 */
		list_for_each(lh, &scsi_host_list) {
			shost = list_entry(lh, struct Scsi_Host, sh_list);
			for (sdev = shost->host_queue; sdev; sdev = sdev->next)
				if (sdev->host->hostt == shost_tp) {
					for (sdev_tp = scsi_devicelist;
					     sdev_tp;
					     sdev_tp = sdev_tp->next)
						if (sdev_tp->attach)
							(*sdev_tp->attach) (sdev);
					if (sdev->attached) {
						scsi_build_commandblocks(sdev);
						if (sdev->current_queue_depth == 0)
							goto out_of_space;
					}
				}
		}

		/* This does any final handling that is required. */
		for (sdev_tp = scsi_devicelist; sdev_tp;
		     sdev_tp = sdev_tp->next) {
			if (sdev_tp->finish && sdev_tp->nr_dev) {
				(*sdev_tp->finish) ();
			}
		}
	}

	return 0;

out_of_space:
	scsi_unregister_host(shost_tp); /* easiest way to clean up?? */
	return 1;
}

/**
 * scsi_unregister_host - unregister a low level host adapter driver
 * @shost_tp:	scsi host template to unregister.
 *
 * Description:
 * 	Similarly, this entry point should be called by a loadable module
 * 	if it is trying to remove a low level scsi driver from the system.
 *
 * Return value:
 * 	0 on Success / 1 on Failure
 *
 * Notes:
 * 	rmmod does not care what we return here the module will be
 * 	removed.
 **/
int scsi_unregister_host(Scsi_Host_Template *shost_tp)
{
	int pcount;

	/* get the big kernel lock, so we don't race with open() */
	lock_kernel();

	pcount = scsi_hosts_registered;

	scsi_tp_for_each_host(shost_tp, scsi_host_chk_and_release);

	if (pcount != scsi_hosts_registered)
		printk(KERN_INFO "scsi : %d host%s left.\n", scsi_hosts_registered,
		       (scsi_hosts_registered == 1) ? "" : "s");

	/*
	 * Remove it from the list if all
	 * hosts were successfully removed (ie preset == 0)
	 */
	if (!shost_tp->present) {
		list_del(&shost_tp->shtp_list);
	}

	MOD_DEC_USE_COUNT;

	unlock_kernel();
	return 0;

}

/**
 * *scsi_host_get_next - get scsi host and inc ref count
 * @shost:	pointer to a Scsi_Host or NULL to start.
 *
 * Return value:
 * 	A pointer to next Scsi_Host in list or NULL.
 **/
struct Scsi_Host *scsi_host_get_next(struct Scsi_Host *shost)
{
	struct list_head *lh = NULL;

	spin_lock(&scsi_host_list_lock);
	if (shost) {
		/* XXX Dec ref on cur shost */
		lh = shost->sh_list.next;
	} else {
		lh = scsi_host_list.next;
	}

	if (lh == &scsi_host_list) {
		shost = (struct Scsi_Host *)NULL;
		goto done;
	}

	shost = list_entry(lh, struct Scsi_Host, sh_list);
	/* XXX Inc ref count */

done:
	spin_unlock(&scsi_host_list_lock);
	return shost;
}

/**
 * scsi_host_hn_get - get a Scsi_Host by host no and inc ref count
 * @host_no:	host number to locate
 *
 * Return value:
 * 	A pointer to located Scsi_Host or NULL.
 **/
struct Scsi_Host *scsi_host_hn_get(unsigned short host_no)
{
	struct list_head *lh;
	struct Scsi_Host *shost;

	spin_lock(&scsi_host_list_lock);
	list_for_each(lh, &scsi_host_list) {
		shost = list_entry(lh, struct Scsi_Host, sh_list);
		if (shost->host_no == host_no) {
			/* XXX Inc ref count */
			goto done;
		}
	}

	shost = (struct Scsi_Host *)NULL;
done:
	spin_unlock(&scsi_host_list_lock);
	return shost;
}

/**
 * *scsi_host_put - dec a Scsi_Host ref count
 * @shost:	Pointer to Scsi_Host to dec.
 **/
void scsi_host_put(struct Scsi_Host *shost)
{

	/* XXX Get list lock */
	/* XXX dec ref count */
	/* XXX Release list lock */
	return;
}

/**
 * scsi_host_hn_init - init scsi host number list from string
 * @shost_hn:	string of scsi host driver names.
 **/
void __init scsi_host_hn_init(char *shost_hn)
{
	char *temp = shost_hn;

	while (temp) {
		while (*temp && (*temp != ':') && (*temp != ','))
			temp++;
		if (!*temp)
			temp = NULL;
		else
			*temp++ = 0;
		(void)scsi_host_hn_add(shost_hn);
		shost_hn = temp;
	}
}

/**
 * scsi_host_no_release - free all entries in scsi host number list
 **/
void __exit scsi_host_hn_release()
{
	struct list_head *lh, *next;
	Scsi_Host_Name *shn;

	list_for_each_safe(lh, next, &scsi_host_hn_list) {
		shn = list_entry(lh, Scsi_Host_Name, shn_list);
		if (shn->name)
			kfree(shn->name);
		kfree(shn);
	}
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
	sdev->device_busy--;
	if (shost->in_recovery && (shost->host_busy == shost->host_failed)) {
		up(shost->eh_wait);
		SCSI_LOG_ERROR_RECOVERY(5, printk("Waking error handler"
					  " thread\n"));
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
}

void scsi_host_failed_inc_and_test(struct Scsi_Host *shost)
{
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	shost->in_recovery = 1;
	shost->host_failed++;
	if (shost->host_busy == shost->host_failed) {
		up(shost->eh_wait);
		SCSI_LOG_ERROR_RECOVERY(5, printk("Waking error handler"
					  " thread\n"));
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
