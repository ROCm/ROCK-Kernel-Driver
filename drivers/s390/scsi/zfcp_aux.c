/*
 *
 * linux/drivers/s390/scsi/zfcp_aux.c
 *
 * FCP adapter driver for IBM eServer zSeries
 *
 * Copyright 2002 IBM Corporation
 * Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *            Raimund Schroeder <raimund.schroeder@de.ibm.com>
 *            Aron Zeh <arzeh@de.ibm.com>
 *            Wolfgang Taphorn <taphorn@de.ibm.com>
 *            Stefan Bader <stefan.bader@de.ibm.com>
 *            Heiko Carstens <heiko.carstens@de.ibm.com>
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
 */

/* this drivers version (do not edit !!! generated and updated by cvs) */
#define ZFCP_AUX_REVISION "$Revision: 1.65 $"

/********************** INCLUDES *********************************************/

#include <linux/init.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>

#include "zfcp_ext.h"

#include <asm/semaphore.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ebcdic.h>
#include <asm/cpcmd.h>		/* Debugging only */
#include <asm/processor.h>	/* Debugging only */

/* accumulated log level (module parameter) */
static u32 loglevel = ZFCP_LOG_LEVEL_DEFAULTS;
/*********************** FUNCTION PROTOTYPES *********************************/

/* written against the module interface */
static int __init  zfcp_module_init(void);
static void __exit zfcp_module_exit(void);

int zfcp_reboot_handler(struct notifier_block *, unsigned long, void *);

/* FCP related */
static void zfcp_nameserver_request_handler(struct zfcp_fsf_req *);

/* miscellaneous */
#ifdef ZFCP_STAT_REQSIZES
static int zfcp_statistics_init_all(void);
static int zfcp_statistics_clear_all(void);
static int zfcp_statistics_clear(struct list_head *);
static int zfcp_statistics_new(struct list_head *, u32);
#endif

/*********************** KERNEL/MODULE PARAMETERS  ***************************/

/* declare driver module init/cleanup functions */
module_init(zfcp_module_init);
module_exit(zfcp_module_exit);

MODULE_AUTHOR("Heiko Carstens <heiko.carstens@de.ibm.com>, "
	      "Martin Peschke <mpeschke@de.ibm.com>, "
	      "Raimund Schroeder <raimund.schroeder@de.ibm.com>, "
	      "Wolfgang Taphorn <taphorn@de.ibm.com>, "
	      "Aron Zeh <arzeh@de.ibm.com>, "
	      "IBM Deutschland Entwicklung GmbH");
/* what this driver module is about */
MODULE_DESCRIPTION
    ("FCP (SCSI over Fibre Channel) HBA driver for IBM eServer zSeries");
MODULE_LICENSE("GPL");
/* log level may be provided as a module parameter */
module_param(loglevel, uint, 0);
/* short explaination of the previous module parameter */
MODULE_PARM_DESC(loglevel,
		 "log levels, 8 nibbles: "
		 "(unassigned) ERP QDIO DIO Config FSF SCSI Other, "
		 "levels: 0=none 1=normal 2=devel 3=trace");

#ifdef ZFCP_PRINT_FLAGS
u32 flags_dump = 0;
module_param(flags_dump, uint, 0);
#endif

/****************************************************************/
/************** Functions without logging ***********************/
/****************************************************************/

void
_zfcp_hex_dump(char *addr, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		printk("%02x", addr[i]);
		if ((i % 4) == 3)
			printk(" ");
		if ((i % 32) == 31)
			printk("\n");
	}
	if ((i % 32) != 31)
		printk("\n");
}

/****************************************************************/
/************** Uncategorised Functions *************************/
/****************************************************************/

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

#ifdef ZFCP_STAT_REQSIZES

static int
zfcp_statistics_clear(struct list_head *head)
{
	int retval = 0;
	unsigned long flags;
	struct zfcp_statistics *stat, *tmp;

	write_lock_irqsave(&zfcp_data.stat_lock, flags);
	list_for_each_entry_safe(stat, tmp, head, list) {
		list_del(&stat->list);
		kfree(stat);
	}
	write_unlock_irqrestore(&zfcp_data.stat_lock, flags);

	return retval;
}

/* Add new statistics entry */
static int
zfcp_statistics_new(struct list_head *head, u32 num)
{
	int retval = 0;
	struct zfcp_statistics *stat;

	stat = kmalloc(sizeof (struct zfcp_statistics), GFP_ATOMIC);
	if (stat) {
		memset(stat, 0, sizeof (struct zfcp_statistics));
		stat->num = num;
		stat->occurrence = 1;
		list_add_tail(&stat->list, head);
	} else
		zfcp_data.stat_errors++;

	return retval;
}

int
zfcp_statistics_inc(struct list_head *head, u32 num)
{
	int retval = 0;
	unsigned long flags;
	struct zfcp_statistics *stat;

	write_lock_irqsave(&zfcp_data.stat_lock, flags);
	list_for_each_entry(stat, head, list) {
		if (stat->num == num) {
			stat->occurrence++;
			goto unlock;
		}
	}
	/* occurrence must be initialized to 1 */
	zfcp_statistics_new(head, num);
 unlock:
	write_unlock_irqrestore(&zfcp_data.stat_lock, flags);
	return retval;
}

static int
zfcp_statistics_init_all(void)
{
	int retval = 0;

	rwlock_init(&zfcp_data.stat_lock);
	INIT_LIST_HEAD(&zfcp_data.read_req_head);
	INIT_LIST_HEAD(&zfcp_data.write_req_head);
	INIT_LIST_HEAD(&zfcp_data.read_sg_head);
	INIT_LIST_HEAD(&zfcp_data.write_sg_head);
	INIT_LIST_HEAD(&zfcp_data.read_sguse_head);
	INIT_LIST_HEAD(&zfcp_data.write_sguse_head);
	return retval;
}

static int
zfcp_statistics_clear_all(void)
{
	int retval = 0;

	zfcp_statistics_clear(&zfcp_data.read_req_head);
	zfcp_statistics_clear(&zfcp_data.write_req_head);
	zfcp_statistics_clear(&zfcp_data.read_sg_head);
	zfcp_statistics_clear(&zfcp_data.write_sg_head);
	zfcp_statistics_clear(&zfcp_data.read_sguse_head);
	zfcp_statistics_clear(&zfcp_data.write_sguse_head);
	return retval;
}

#endif /* ZFCP_STAT_REQSIZES */

static inline int
zfcp_fsf_req_is_scsi_cmnd(struct zfcp_fsf_req *fsf_req)
{
	return ((fsf_req->fsf_command == FSF_QTCB_FCP_CMND) &&
		!(fsf_req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT));
}

void
zfcp_cmd_dbf_event_fsf(const char *text, struct zfcp_fsf_req *fsf_req,
		       void *add_data, int add_length)
{
#ifdef ZFCP_DEBUG_COMMANDS
	struct zfcp_adapter *adapter = fsf_req->adapter;
	Scsi_Cmnd *scsi_cmnd;
	int level = 3;
	int i;
	unsigned long flags;

	write_lock_irqsave(&adapter->cmd_dbf_lock, flags);
	if (zfcp_fsf_req_is_scsi_cmnd(fsf_req)) {
		scsi_cmnd = fsf_req->data.send_fcp_command_task.scsi_cmnd;
		debug_text_event(adapter->cmd_dbf, level, "fsferror");
		debug_text_event(adapter->cmd_dbf, level, text);
		debug_event(adapter->cmd_dbf, level, &fsf_req,
			    sizeof (unsigned long));
		debug_event(adapter->cmd_dbf, level, &fsf_req->seq_no,
			    sizeof (u32));
		debug_event(adapter->cmd_dbf, level, &scsi_cmnd,
			    sizeof (unsigned long));
		for (i = 0; i < add_length; i += ZFCP_CMD_DBF_LENGTH)
			debug_event(adapter->cmd_dbf,
				    level,
				    (char *) add_data + i,
				    min(ZFCP_CMD_DBF_LENGTH, add_length - i));
	}
	write_unlock_irqrestore(&adapter->cmd_dbf_lock, flags);
#endif
}

void
zfcp_cmd_dbf_event_scsi(const char *text, Scsi_Cmnd * scsi_cmnd)
{
#ifdef ZFCP_DEBUG_COMMANDS
	struct zfcp_adapter *adapter;
	union zfcp_req_data *req_data;
	struct zfcp_fsf_req *fsf_req;
	int level = ((host_byte(scsi_cmnd->result) != 0) ? 1 : 5);
	unsigned long flags;

	adapter = (struct zfcp_adapter *) scsi_cmnd->device->host->hostdata[0];
	req_data = (union zfcp_req_data *) scsi_cmnd->host_scribble;
	fsf_req = (req_data ? req_data->send_fcp_command_task.fsf_req : NULL);
	write_lock_irqsave(&adapter->cmd_dbf_lock, flags);
	debug_text_event(adapter->cmd_dbf, level, "hostbyte");
	debug_text_event(adapter->cmd_dbf, level, text);
	debug_event(adapter->cmd_dbf, level, &scsi_cmnd->result, sizeof (u32));
	debug_event(adapter->cmd_dbf, level, &scsi_cmnd,
		    sizeof (unsigned long));
	if (fsf_req) {
		debug_event(adapter->cmd_dbf, level, &fsf_req,
			    sizeof (unsigned long));
		debug_event(adapter->cmd_dbf, level, &fsf_req->seq_no,
			    sizeof (u32));
	} else {
		debug_text_event(adapter->cmd_dbf, level, "");
		debug_text_event(adapter->cmd_dbf, level, "");
	}
	write_unlock_irqrestore(&adapter->cmd_dbf_lock, flags);
#endif
}

void
zfcp_in_els_dbf_event(struct zfcp_adapter *adapter, const char *text,
		      struct fsf_status_read_buffer *status_buffer, int length)
{
#ifdef ZFCP_DEBUG_INCOMING_ELS
	int level = 1;
	int i;

	debug_text_event(adapter->in_els_dbf, level, text);
	debug_event(adapter->in_els_dbf, level, &status_buffer->d_id, 8);
	for (i = 0; i < length; i += ZFCP_IN_ELS_DBF_LENGTH)
		debug_event(adapter->in_els_dbf,
			    level,
			    (char *) status_buffer->payload + i,
			    min(ZFCP_IN_ELS_DBF_LENGTH, length - i));
#endif
}

static int __init
zfcp_module_init(void)
{

	int retval = 0;

	atomic_set(&zfcp_data.loglevel, loglevel);

	ZFCP_LOG_DEBUG(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");

	ZFCP_LOG_TRACE("Start Address of module: 0x%lx\n",
		       (unsigned long) &zfcp_module_init);

	/* initialize adapter list */
	INIT_LIST_HEAD(&zfcp_data.adapter_list_head);

	/* initialize adapters to be removed list head */
	INIT_LIST_HEAD(&zfcp_data.adapter_remove_lh);

#ifdef ZFCP_STAT_REQSIZES
	zfcp_statistics_init_all();
#endif

	/* Initialise proc semaphores */
	sema_init(&zfcp_data.config_sema, 1);

	/* initialise configuration rw lock */
	rwlock_init(&zfcp_data.config_lock);

	zfcp_data.reboot_notifier.notifier_call = zfcp_reboot_handler;
	register_reboot_notifier(&zfcp_data.reboot_notifier);

	/* save address of data structure managing the driver module */
	zfcp_data.scsi_host_template.module = THIS_MODULE;

	/* setup dynamic I/O */
	retval = zfcp_ccw_register();
	if (retval) {
		ZFCP_LOG_NORMAL("Registering with common I/O layer failed.\n");
		goto out_ccw_register;
	}
	goto out;

 out_ccw_register:
#ifdef ZFCP_STAT_REQSIZES
	zfcp_statistics_clear_all();
#endif

 out:
	return retval;
}

static void __exit
zfcp_module_exit(void)
{
	unregister_reboot_notifier(&zfcp_data.reboot_notifier);
	zfcp_ccw_unregister();
#ifdef ZFCP_STAT_REQSIZES
	zfcp_statistics_clear_all();
#endif
	ZFCP_LOG_DEBUG("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
}

/*
 * This function is called automatically by the kernel whenever a reboot or a 
 * shut-down is initiated and zfcp is still loaded
 *
 * locks:       zfcp_data.config_sema is taken prior to shutting down the module
 *              and removing all structures
 * returns:     NOTIFY_DONE in all cases
 */
int
zfcp_reboot_handler(struct notifier_block *notifier, unsigned long code,
		    void *ptr)
{
	int retval = NOTIFY_DONE;

	/* block access to config (for rest of lifetime of this Linux) */
	down(&zfcp_data.config_sema);
	zfcp_erp_adapter_shutdown_all();

	return retval;
}

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX

/****************************************************************/
/****** Functions for configuration/set-up of structures ********/
/****************************************************************/

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

#ifndef MODULE
/* zfcp_loglevel boot_parameter */
static int __init
zfcp_loglevel_setup(char *str)
{
	loglevel = simple_strtoul(str, NULL, 0);
	ZFCP_LOG_TRACE("loglevel is 0x%x\n", loglevel);
	return 1;		/* why just 1? */
}

__setup("zfcp_loglevel=", zfcp_loglevel_setup);
#endif				/* not MODULE */

/**
 * zfcp_get_unit_by_lun - find unit in unit list of port by fcp lun
 * @port: pointer to port to search for unit
 * @fcp_lun: lun to search for
 * Traverses list of all units of a port and returns pointer to a unit
 * if lun of a unit matches.
 */

struct zfcp_unit *
zfcp_get_unit_by_lun(struct zfcp_port *port, fcp_lun_t fcp_lun)
{
	struct zfcp_unit *unit;
	int found = 0;

	list_for_each_entry(unit, &port->unit_list_head, list) {
		if ((unit->fcp_lun == fcp_lun) &&
		    !atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status))
		{
			found = 1;
			break;
		}
	}
	return found ? unit : NULL;
}

/**
 * zfcp_get_port_by_wwpn - find unit in unit list of port by fcp lun
 * @adapter: pointer to adapter to search for port
 * @wwpn: wwpn to search for
 * Traverses list of all ports of an adapter and returns a pointer to a port
 * if wwpn of a port matches.
 */

struct zfcp_port *
zfcp_get_port_by_wwpn(struct zfcp_adapter *adapter, wwn_t wwpn)
{
	struct zfcp_port *port;
	int found = 0;

	list_for_each_entry(port, &adapter->port_list_head, list) {
		if ((port->wwpn == wwpn) &&
		    !atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status))
		{
			found = 1;
			break;
		}
	}
	return found ? port : NULL;
}

/*
 * Enqueues a logical unit at the end of the unit list associated with the 
 * specified port. Also sets up some unit internal structures.
 *
 * returns:	pointer to unit with a usecount of 1 if a new unit was
 *              successfully enqueued
 *              NULL otherwise
 * locks:	config_sema must be held to serialise changes to the unit list
 */
struct zfcp_unit *
zfcp_unit_enqueue(struct zfcp_port *port, fcp_lun_t fcp_lun)
{
	struct zfcp_unit *unit;

	/*
	 * check that there is no unit with this FCP_LUN already in list
	 * and enqueue it.
	 * Note: Unlike for the adapter and the port, this is an error
	 */
	read_lock_irq(&zfcp_data.config_lock);
	unit = zfcp_get_unit_by_lun(port, fcp_lun);
	read_unlock_irq(&zfcp_data.config_lock);
	if (unit)
		return NULL;

	unit = kmalloc(sizeof (struct zfcp_unit), GFP_KERNEL);
	if (!unit)
		return NULL;
	memset(unit, 0, sizeof (struct zfcp_unit));

	/* initialise reference count stuff */
	atomic_set(&unit->refcount, 0);
	init_waitqueue_head(&unit->remove_wq);

	unit->port = port;
	/*
	 * FIXME: reuse of scsi_luns!
	 */
	unit->scsi_lun = port->max_scsi_lun + 1;
	unit->fcp_lun = fcp_lun;
	unit->common_magic = ZFCP_MAGIC;
	unit->specific_magic = ZFCP_MAGIC_UNIT;

	/* setup for sysfs registration */
	snprintf(unit->sysfs_device.bus_id, BUS_ID_SIZE, "0x%016llx", fcp_lun);
	unit->sysfs_device.parent = &port->sysfs_device;
	unit->sysfs_device.release = zfcp_sysfs_unit_release;
	dev_set_drvdata(&unit->sysfs_device, unit);

	/* mark unit unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);

	if (device_register(&unit->sysfs_device)) {
		kfree(unit);
		return NULL;
	}

	if (zfcp_sysfs_unit_create_files(&unit->sysfs_device)) {
		/*
		 * failed to create all sysfs attributes, therefore the unit
		 * must be put on the unit_remove listhead of the port where
		 * the release function expects it.
		 */
		write_lock_irq(&zfcp_data.config_lock);
		list_add_tail(&unit->list, &port->unit_remove_lh);
		write_unlock_irq(&zfcp_data.config_lock);
		device_unregister(&unit->sysfs_device);
		return NULL;
	}

	/*
	 * update max SCSI LUN of logical units attached to parent remote port
	 */
	port->max_scsi_lun++;

	/*
	 * update max SCSI LUN of logical units attached to parent adapter
	 */
	if (port->adapter->max_scsi_lun < port->max_scsi_lun)
		port->adapter->max_scsi_lun = port->max_scsi_lun;

	/*
	 * update max SCSI LUN of logical units attached to host (SCSI stack)
	 */
	if (port->adapter->scsi_host &&
	    (port->adapter->scsi_host->max_lun < port->max_scsi_lun))
		port->adapter->scsi_host->max_lun = port->max_scsi_lun + 1;

	zfcp_unit_get(unit);

	/* unit is new and needs to be added to list */
	write_lock_irq(&zfcp_data.config_lock);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &unit->status);
	list_add_tail(&unit->list, &port->unit_list_head);
	write_unlock_irq(&zfcp_data.config_lock);

	port->units++;

	return unit;
}

/* locks:  config_sema must be held */
void
zfcp_unit_dequeue(struct zfcp_unit *unit)
{
	/* remove specified unit data structure from list */
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&unit->list);
	write_unlock_irq(&zfcp_data.config_lock);

	unit->port->units--;
	zfcp_port_put(unit->port);

	kfree(unit);

	return;
}

static void *
zfcp_mempool_alloc(int gfp_mask, void *size)
{
	return kmalloc((size_t) size, gfp_mask);
}

static void
zfcp_mempool_free(void *element, void *size)
{
	kfree(element);
}

/*
 * Allocates a combined QTCB/fsf_req buffer for erp actions and fcp/SCSI
 * commands.
 * It also genrates fcp-nameserver request/response buffer and unsolicited 
 * status read fsf_req buffers.
 *
 * locks:       must only be called with zfcp_data.config_sema taken
 */
static int
zfcp_allocate_low_mem_buffers(struct zfcp_adapter *adapter)
{
	adapter->pool.erp_fsf = mempool_create(
		1,
		zfcp_mempool_alloc,
		zfcp_mempool_free,
		(void *) ZFCP_QTCB_AND_REQ_SIZE);
	if (!adapter->pool.erp_fsf) {
		ZFCP_LOG_INFO
		    ("error: FCP command buffer pool allocation failed\n");
		return -ENOMEM;
	}

	adapter->pool.nameserver = mempool_create(
		1,
		zfcp_mempool_alloc,
		zfcp_mempool_free,
		(void *) (2 *  sizeof (struct fc_ct_iu)));
	if (!adapter->pool.nameserver) {
		ZFCP_LOG_INFO
		    ("error: Nameserver buffer pool allocation failed\n");
		return -ENOMEM;
	}

	adapter->pool.status_read_fsf = mempool_create(
		ZFCP_STATUS_READS_RECOM,
		zfcp_mempool_alloc,
		zfcp_mempool_free,
		(void *) sizeof (struct zfcp_fsf_req));
	if (!adapter->pool.status_read_fsf) {
		ZFCP_LOG_INFO
		    ("error: Status read request pool allocation failed\n");
		return -ENOMEM;
	}

	adapter->pool.status_read_buf = mempool_create(
		ZFCP_STATUS_READS_RECOM,
		zfcp_mempool_alloc,
		zfcp_mempool_free,
		(void *) sizeof (struct	fsf_status_read_buffer));
	if (!adapter->pool.status_read_buf) {
		ZFCP_LOG_INFO
		    ("error: Status read buffer pool allocation failed\n");
		return -ENOMEM;
	}

	adapter->pool.fcp_command_fsf = mempool_create(
		1,
		zfcp_mempool_alloc,
		zfcp_mempool_free,
		(void *)
		ZFCP_QTCB_AND_REQ_SIZE);
	if (!adapter->pool.fcp_command_fsf) {
		ZFCP_LOG_INFO
		    ("error: FCP command buffer pool allocation failed\n");
		return -ENOMEM;
	}
	init_timer(&adapter->pool.fcp_command_fsf_timer);
	adapter->pool.fcp_command_fsf_timer.function =
	    zfcp_erp_scsi_low_mem_buffer_timeout_handler;
	adapter->pool.fcp_command_fsf_timer.data = (unsigned long) adapter;

	return 0;
}

/* locks:       must only be called with zfcp_data.config_sema taken */
static void
zfcp_free_low_mem_buffers(struct zfcp_adapter *adapter)
{
	if (adapter->pool.status_read_fsf)
		mempool_destroy(adapter->pool.status_read_fsf);
	if (adapter->pool.status_read_buf)
		mempool_destroy(adapter->pool.status_read_buf);
	if (adapter->pool.nameserver)
		mempool_destroy(adapter->pool.nameserver);
	if (adapter->pool.erp_fsf)
		mempool_destroy(adapter->pool.erp_fsf);
	if (adapter->pool.fcp_command_fsf)
		mempool_destroy(adapter->pool.fcp_command_fsf);
}

/*
 * Enqueues an adapter at the end of the adapter list in the driver data.
 * All adapter internal structures are set up.
 * Proc-fs entries are also created.
 *
 * returns:	0             if a new adapter was successfully enqueued
 *              ZFCP_KNOWN    if an adapter with this devno was already present
 *		-ENOMEM       if alloc failed
 * locks:	config_sema must be held to serialise changes to the adapter list
 */
struct zfcp_adapter *
zfcp_adapter_enqueue(struct ccw_device *ccw_device)
{
	int retval = 0;
	struct zfcp_adapter *adapter;
	char dbf_name[20];

	/*
	 * Note: It is safe to release the list_lock, as any list changes 
	 * are protected by the config_sema, which must be held to get here
	 */

	/* try to allocate new adapter data structure (zeroed) */
	adapter = kmalloc(sizeof (struct zfcp_adapter), GFP_KERNEL);
	if (!adapter) {
		ZFCP_LOG_INFO("error: Allocation of base adapter "
			      "structure failed\n");
		goto out;
	}
	memset(adapter, 0, sizeof (struct zfcp_adapter));

	ccw_device->handler = NULL;

	/* save ccw_device pointer */
	adapter->ccw_device = ccw_device;

	retval = zfcp_qdio_allocate_queues(adapter);
	if (retval)
		goto queues_alloc_failed;

	retval = zfcp_qdio_allocate(adapter);
	if (retval)
		goto qdio_allocate_failed;

	retval = zfcp_allocate_low_mem_buffers(adapter);
	if (retval)
		goto failed_low_mem_buffers;

	/* set magics */
	adapter->common_magic = ZFCP_MAGIC;
	adapter->specific_magic = ZFCP_MAGIC_ADAPTER;

	/* initialise reference count stuff */
	atomic_set(&adapter->refcount, 0);
	init_waitqueue_head(&adapter->remove_wq);

	/* initialise list of ports */
	INIT_LIST_HEAD(&adapter->port_list_head);

	/* initialise list of ports to be removed */
	INIT_LIST_HEAD(&adapter->port_remove_lh);

	/* initialize list of fsf requests */
	rwlock_init(&adapter->fsf_req_list_lock);
	INIT_LIST_HEAD(&adapter->fsf_req_list_head);

	/* initialize abort lock */
	rwlock_init(&adapter->abort_lock);

	/* initialise scsi faking structures */
	rwlock_init(&adapter->fake_list_lock);
	init_timer(&adapter->fake_scsi_timer);

	/* initialise some erp stuff */
	init_waitqueue_head(&adapter->erp_thread_wqh);
	init_waitqueue_head(&adapter->erp_done_wqh);

	/* notification when there are no outstanding SCSI commands */
	init_waitqueue_head(&adapter->scsi_reqs_active_wq);

	/* initialize lock of associated request queue */
	rwlock_init(&adapter->request_queue.queue_lock);

	/* intitialise SCSI ER timer */
	init_timer(&adapter->scsi_er_timer);

	/* set FC service class used per default */
	adapter->fc_service_class = ZFCP_FC_SERVICE_CLASS_DEFAULT;

	sprintf(adapter->name, "%s", zfcp_get_busid_by_adapter(adapter));
	ASCEBC(adapter->name, strlen(adapter->name));

	/* mark adapter unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);

	adapter->ccw_device = ccw_device;
	dev_set_drvdata(&ccw_device->dev, adapter);

	if (zfcp_sysfs_adapter_create_files(&ccw_device->dev))
		goto sysfs_failed;

#ifdef ZFCP_DEBUG_REQUESTS
	/* debug feature area which records fsf request sequence numbers */
	sprintf(dbf_name, ZFCP_REQ_DBF_NAME "0x%s",
		zfcp_get_busid_by_adapter(adapter));
	adapter->req_dbf = debug_register(dbf_name,
					  ZFCP_REQ_DBF_INDEX,
					  ZFCP_REQ_DBF_AREAS,
					  ZFCP_REQ_DBF_LENGTH);
	if (!adapter->req_dbf) {
		ZFCP_LOG_INFO
		    ("error: Out of resources. Request debug feature for "
		     "adapter %s could not be generated.\n",
		     zfcp_get_busid_by_adapter(adapter));
		retval = -ENOMEM;
		goto failed_req_dbf;
	}
	debug_register_view(adapter->req_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->req_dbf, ZFCP_REQ_DBF_LEVEL);
	debug_text_event(adapter->req_dbf, 1, "zzz");
#endif				/* ZFCP_DEBUG_REQUESTS */

#ifdef ZFCP_DEBUG_COMMANDS
	/* debug feature area which records SCSI command failures (hostbyte) */
	rwlock_init(&adapter->cmd_dbf_lock);
	sprintf(dbf_name, ZFCP_CMD_DBF_NAME "%s",
		zfcp_get_busid_by_adapter(adapter));
	adapter->cmd_dbf = debug_register(dbf_name,
					  ZFCP_CMD_DBF_INDEX,
					  ZFCP_CMD_DBF_AREAS,
					  ZFCP_CMD_DBF_LENGTH);
	if (!adapter->cmd_dbf) {
		ZFCP_LOG_INFO
		    ("error: Out of resources. Command debug feature for "
		     "adapter %s could not be generated.\n",
		     zfcp_get_busid_by_adapter(adapter));
		retval = -ENOMEM;
		goto failed_cmd_dbf;
	}
	debug_register_view(adapter->cmd_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->cmd_dbf, ZFCP_CMD_DBF_LEVEL);
#endif				/* ZFCP_DEBUG_COMMANDS */

#ifdef ZFCP_DEBUG_ABORTS
	/* debug feature area which records SCSI command aborts */
	sprintf(dbf_name, ZFCP_ABORT_DBF_NAME "%s",
		zfcp_get_busid_by_adapter(adapter));
	adapter->abort_dbf = debug_register(dbf_name,
					    ZFCP_ABORT_DBF_INDEX,
					    ZFCP_ABORT_DBF_AREAS,
					    ZFCP_ABORT_DBF_LENGTH);
	if (!adapter->abort_dbf) {
		ZFCP_LOG_INFO
		    ("error: Out of resources. Abort debug feature for "
		     "adapter %s could not be generated.\n",
		     zfcp_get_busid_by_adapter(adapter));
		retval = -ENOMEM;
		goto failed_abort_dbf;
	}
	debug_register_view(adapter->abort_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->abort_dbf, ZFCP_ABORT_DBF_LEVEL);
#endif				/* ZFCP_DEBUG_ABORTS */

#ifdef ZFCP_DEBUG_INCOMING_ELS
	/* debug feature area which records SCSI command aborts */
	sprintf(dbf_name, ZFCP_IN_ELS_DBF_NAME "%s",
		zfcp_get_busid_by_adapter(adapter));
	adapter->in_els_dbf = debug_register(dbf_name,
					     ZFCP_IN_ELS_DBF_INDEX,
					     ZFCP_IN_ELS_DBF_AREAS,
					     ZFCP_IN_ELS_DBF_LENGTH);
	if (!adapter->in_els_dbf) {
		ZFCP_LOG_INFO("error: Out of resources. ELS debug feature for "
			      "adapter %s could not be generated.\n",
			      zfcp_get_busid_by_adapter(adapter));
		retval = -ENOMEM;
		goto failed_in_els_dbf;
	}
	debug_register_view(adapter->in_els_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->in_els_dbf, ZFCP_IN_ELS_DBF_LEVEL);
#endif				/* ZFCP_DEBUG_INCOMING_ELS */

	sprintf(dbf_name, ZFCP_ERP_DBF_NAME "%s",
		zfcp_get_busid_by_adapter(adapter));
	adapter->erp_dbf = debug_register(dbf_name,
					  ZFCP_ERP_DBF_INDEX,
					  ZFCP_ERP_DBF_AREAS,
					  ZFCP_ERP_DBF_LENGTH);
	if (!adapter->erp_dbf) {
		ZFCP_LOG_INFO("error: Out of resources. ERP debug feature for "
			      "adapter %s could not be generated.\n",
			      zfcp_get_busid_by_adapter(adapter));
		retval = -ENOMEM;
		goto failed_erp_dbf;
	}
	debug_register_view(adapter->erp_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->erp_dbf, ZFCP_ERP_DBF_LEVEL);

	retval = zfcp_erp_thread_setup(adapter);
	if (retval) {
		ZFCP_LOG_INFO("error: out of resources. "
			      "error recovery thread for the adapter %s "
			      "could not be started\n",
			      zfcp_get_busid_by_adapter(adapter));
		goto thread_failed;
	}

	/* put allocated adapter at list tail */
	write_lock_irq(&zfcp_data.config_lock);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &adapter->status);
	list_add_tail(&adapter->list, &zfcp_data.adapter_list_head);
	write_unlock_irq(&zfcp_data.config_lock);

	zfcp_data.adapters++;

	goto out;

 thread_failed:
	if (qdio_free(adapter->ccw_device) != 0)
		ZFCP_LOG_NORMAL
		    ("bug: could not free memory used by data transfer "
		     "mechanism for adapter %s\n",
		     zfcp_get_busid_by_adapter(adapter));

	debug_unregister(adapter->erp_dbf);

 failed_erp_dbf:
#ifdef ZFCP_DEBUG_INCOMING_ELS
	debug_unregister(adapter->in_els_dbf);
 failed_in_els_dbf:
#endif

#ifdef ZFCP_DEBUG_ABORTS
	debug_unregister(adapter->abort_dbf);
 failed_abort_dbf:
#endif

#ifdef ZFCP_DEBUG_COMMANDS
	debug_unregister(adapter->cmd_dbf);
 failed_cmd_dbf:
#endif

#ifdef ZFCP_DEBUG_REQUESTS
	debug_unregister(adapter->req_dbf);
 failed_req_dbf:
#endif
	zfcp_sysfs_adapter_remove_files(&ccw_device->dev);
 sysfs_failed:
	dev_set_drvdata(&ccw_device->dev, NULL);
 failed_low_mem_buffers:
	zfcp_free_low_mem_buffers(adapter);
	qdio_free(ccw_device);
 qdio_allocate_failed:
	zfcp_qdio_free_queues(adapter);
 queues_alloc_failed:
	kfree(adapter);
	adapter = NULL;
 out:
	return adapter;
}

/*
 * returns:	0 - struct zfcp_adapter  data structure successfully removed
 *		!0 - struct zfcp_adapter  data structure could not be removed
 *			(e.g. still used)
 * locks:	adapter list write lock is assumed to be held by caller
 *              adapter->fsf_req_list_lock is taken and released within this 
 *              function and must not be held on entry
 */
void
zfcp_adapter_dequeue(struct zfcp_adapter *adapter)
{
	int retval = 0;
	unsigned long flags;

	zfcp_sysfs_adapter_remove_files(&adapter->ccw_device->dev);
	dev_set_drvdata(&adapter->ccw_device->dev, NULL);
	/* sanity check: no pending FSF requests */
	read_lock_irqsave(&adapter->fsf_req_list_lock, flags);
	retval = !list_empty(&adapter->fsf_req_list_head);
	read_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);
	if (retval) {
		ZFCP_LOG_NORMAL("bug: Adapter %s is still in use, "
				"%i requests are still outstanding "
				"(debug info 0x%lx)\n",
				zfcp_get_busid_by_adapter(adapter),
				atomic_read(&adapter->fsf_reqs_active),
				(unsigned long) adapter);
		retval = -EBUSY;
		goto out;
	}

	/* remove specified adapter data structure from list */
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&adapter->list);
	write_unlock_irq(&zfcp_data.config_lock);

	/* decrease number of adapters in list */
	zfcp_data.adapters--;

	ZFCP_LOG_TRACE("adapter 0x%lx removed from list, "
		       "%i adapters still in list\n",
		       (unsigned long) adapter, zfcp_data.adapters);

	retval = zfcp_erp_thread_kill(adapter);

	retval |= qdio_free(adapter->ccw_device);
	if (retval)
		ZFCP_LOG_NORMAL
		    ("bug: could not free memory used by data transfer "
		     "mechanism for adapter %s\n",
		     zfcp_get_busid_by_adapter(adapter));

	debug_unregister(adapter->erp_dbf);

#ifdef ZFCP_DEBUG_REQUESTS
	debug_unregister(adapter->req_dbf);
#endif

#ifdef ZFCP_DEBUG_COMMANDS
	debug_unregister(adapter->cmd_dbf);
#endif
#ifdef ZFCP_DEBUG_ABORTS
	debug_unregister(adapter->abort_dbf);
#endif

#ifdef ZFCP_DEBUG_INCOMING_ELS
	debug_unregister(adapter->in_els_dbf);
#endif

	zfcp_free_low_mem_buffers(adapter);
	/* free memory of adapter data structure and queues */
	zfcp_qdio_free_queues(adapter);
	ZFCP_LOG_TRACE("Freeing adapter structure.\n");
	kfree(adapter);
 out:
	return;
}

/*
 * Enqueues a remote port at the end of the port list.
 * All port internal structures are set-up and the proc-fs entry is also 
 * allocated. Some SCSI-stack structures are modified for the port.
 *
 * returns:	0            if a new port was successfully enqueued
 *              ZFCP_KNOWN   if a port with the requested wwpn already exists
 *              -ENOMEM      if allocation failed
 *              -EINVAL      if at least one of the specified parameters was wrong
 * locks:       config_sema must be held to serialise changes to the port list
 *              within this function (must not be held on entry)
 */
struct zfcp_port *
zfcp_port_enqueue(struct zfcp_adapter *adapter, wwn_t wwpn, u32 status)
{
	struct zfcp_port *port;
	int check_scsi_id;
	int check_wwpn;

	check_scsi_id = !(status & ZFCP_STATUS_PORT_NO_SCSI_ID);
	check_wwpn = !(status & ZFCP_STATUS_PORT_NO_WWPN);

	/*
	 * check that there is no port with this WWPN already in list
	 */
	if (check_wwpn) {
		read_lock_irq(&zfcp_data.config_lock);
		port = zfcp_get_port_by_wwpn(adapter, wwpn);
		read_unlock_irq(&zfcp_data.config_lock);
		if (port)
			return NULL;
	}

	port = kmalloc(sizeof (struct zfcp_port), GFP_KERNEL);
	if (!port)
		return NULL;
	memset(port, 0, sizeof (struct zfcp_port));

	/* initialise reference count stuff */
	atomic_set(&port->refcount, 0);
	init_waitqueue_head(&port->remove_wq);

	INIT_LIST_HEAD(&port->unit_list_head);
	INIT_LIST_HEAD(&port->unit_remove_lh);

	port->adapter = adapter;

	if (check_scsi_id)
		port->scsi_id = adapter->max_scsi_id + 1;

	if (check_wwpn)
		port->wwpn = wwpn;

	atomic_set_mask(status, &port->status);

	port->common_magic = ZFCP_MAGIC;
	port->specific_magic = ZFCP_MAGIC_PORT;

	/* setup for sysfs registration */
	if (status & ZFCP_STATUS_PORT_NAMESERVER)
		snprintf(port->sysfs_device.bus_id, BUS_ID_SIZE, "nameserver");
	else
		snprintf(port->sysfs_device.bus_id,
			 BUS_ID_SIZE, "0x%016llx", wwpn);
	port->sysfs_device.parent = &adapter->ccw_device->dev;
	port->sysfs_device.release = zfcp_sysfs_port_release;
	dev_set_drvdata(&port->sysfs_device, port);

	/* mark port unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);

	if (device_register(&port->sysfs_device)) {
		kfree(port);
		return NULL;
	}

	if (zfcp_sysfs_port_create_files(&port->sysfs_device, status)) {
		/*
		 * failed to create all sysfs attributes, therefore the port
		 * must be put on the port_remove listhead of the adapter
		 * where the release function expects it.
		 */
		write_lock_irq(&zfcp_data.config_lock);
		list_add_tail(&port->list, &adapter->port_remove_lh);
		write_unlock_irq(&zfcp_data.config_lock);
		device_unregister(&port->sysfs_device);
		return NULL;
	}

	if (check_scsi_id) {
		/*
		 * update max. SCSI ID of remote ports attached to
		 * "parent" adapter if necessary
		 * (do not care about the adapters own SCSI ID)
		 */
		adapter->max_scsi_id++;

		/*
		 * update max. SCSI ID of remote ports attached to
		 * "parent" host (SCSI stack) if necessary
		 */
		if (adapter->scsi_host &&
		    (adapter->scsi_host->max_id < adapter->max_scsi_id + 1))
			adapter->scsi_host->max_id = adapter->max_scsi_id + 1;
	}

	zfcp_port_get(port);

	write_lock_irq(&zfcp_data.config_lock);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &port->status);
	list_add_tail(&port->list, &adapter->port_list_head);
	write_unlock_irq(&zfcp_data.config_lock);

	adapter->ports++;

	return port;
}

/*
 * returns:	0 - struct zfcp_port data structure successfully removed
 *		!0 - struct zfcp_port data structure could not be removed
 *			(e.g. still used)
 * locks :	port list write lock is assumed to be held by caller
 */
void
zfcp_port_dequeue(struct zfcp_port *port)
{
	/* remove specified port from list */
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&port->list);
	write_unlock_irq(&zfcp_data.config_lock);

	port->adapter->ports--;
	zfcp_adapter_put(port->adapter);

	kfree(port);

	return;
}

/* Enqueues a nameserver port */
int
zfcp_nameserver_enqueue(struct zfcp_adapter *adapter)
{
	struct zfcp_port *port;

	/* generate port structure */
	port = zfcp_port_enqueue(adapter, 0, ZFCP_STATUS_PORT_NAMESERVER);
	if (!port) {
		ZFCP_LOG_INFO("error: Could not establish a connection to the "
			      "fabric name server connected to the "
			      "adapter %s\n",
			      zfcp_get_busid_by_adapter(adapter));
		return -ENXIO;
	}
	/* set special D_ID */
	port->d_id = ZFCP_DID_NAMESERVER;
	adapter->nameserver_port = port;
	zfcp_adapter_get(adapter);
	zfcp_port_put(port);

	return 0;
}

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX

/****************************************************************/
/******* Fibre Channel Standard related Functions  **************/
/****************************************************************/

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FC
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FC

void
zfcp_fsf_incoming_els_rscn(struct zfcp_adapter *adapter,
			   struct fsf_status_read_buffer *status_buffer)
{
	struct fcp_rscn_head *fcp_rscn_head;
	struct fcp_rscn_element *fcp_rscn_element;
	struct zfcp_port *port;
	int i;
	int reopen_unknown = 0;
	int no_entries;
	unsigned long flags;

	fcp_rscn_head = (struct fcp_rscn_head *) status_buffer->payload;
	fcp_rscn_element = (struct fcp_rscn_element *) status_buffer->payload;

	/* see FC-FS */
	no_entries = (fcp_rscn_head->payload_len / 4);

	zfcp_in_els_dbf_event(adapter, "##rscn", status_buffer,
			      fcp_rscn_head->payload_len);

	debug_text_event(adapter->erp_dbf, 1, "unsol_els_rscn:");
	for (i = 1; i < no_entries; i++) {
		int known;
		int range_mask;
		int no_notifications;

		range_mask = 0;
		no_notifications = 0;
		known = 0;
		/* skip head and start with 1st element */
		fcp_rscn_element++;
		switch (fcp_rscn_element->addr_format) {
		case ZFCP_PORT_ADDRESS:
			ZFCP_LOG_FLAGS(1, "ZFCP_PORT_ADDRESS\n");
			range_mask = ZFCP_PORTS_RANGE_PORT;
			no_notifications = 1;
			break;
		case ZFCP_AREA_ADDRESS:
			ZFCP_LOG_FLAGS(1, "ZFCP_AREA_ADDRESS\n");
			/* skip head and start with 1st element */
			range_mask = ZFCP_PORTS_RANGE_AREA;
			no_notifications = ZFCP_NO_PORTS_PER_AREA;
			break;
		case ZFCP_DOMAIN_ADDRESS:
			ZFCP_LOG_FLAGS(1, "ZFCP_DOMAIN_ADDRESS\n");
			range_mask = ZFCP_PORTS_RANGE_DOMAIN;
			no_notifications = ZFCP_NO_PORTS_PER_DOMAIN;
			break;
		case ZFCP_FABRIC_ADDRESS:
			ZFCP_LOG_FLAGS(1, "ZFCP_FABRIC_ADDRESS\n");
			range_mask = ZFCP_PORTS_RANGE_FABRIC;
			no_notifications = ZFCP_NO_PORTS_PER_FABRIC;
			break;
		default:
			/* cannot happen */
			break;
		}
		read_lock_irqsave(&zfcp_data.config_lock, flags);
		list_for_each_entry(port, &adapter->port_list_head, list) {
			/* Do we know this port? If not skip it. */
			if (!atomic_test_mask
			    (ZFCP_STATUS_PORT_DID_DID, &port->status))
				continue;
			/*
			 * FIXME: race: d_id might being invalidated
			 * (...DID_DID reset)
			 */
			if ((port->d_id & range_mask)
			    == (fcp_rscn_element->nport_did & range_mask)) {
				known++;
				ZFCP_LOG_TRACE("known=%d, reopen did 0x%x\n",
					       known,
					       fcp_rscn_element->nport_did);
				/*
				 * Unfortunately, an RSCN does not specify the
				 * type of change a target underwent. We assume
				 * that it makes sense to reopen the link.
				 * FIXME: Shall we try to find out more about
				 * the target and link state before closing it?
				 * How to accomplish this? (nameserver?)
				 * Where would such code be put in?
				 * (inside or outside erp)
				 */
				ZFCP_LOG_INFO
				    ("Received state change notification."
				     "Trying to reopen the port with wwpn "
				     "0x%Lx.\n", port->wwpn);
				debug_text_event(adapter->erp_dbf, 1,
						 "unsol_els_rscnk:");
				zfcp_erp_port_reopen(port, 0);
			}
		}
		read_unlock_irqrestore(&zfcp_data.config_lock, flags);
		ZFCP_LOG_TRACE("known %d, no_notifications %d\n",
			       known, no_notifications);
		if (known < no_notifications) {
			ZFCP_LOG_DEBUG
			    ("At least one unknown port changed state. "
			     "Unknown ports need to be reopened.\n");
			reopen_unknown = 1;
		}
	}			// for (i=1; i < no_entries; i++)

	if (reopen_unknown) {
		ZFCP_LOG_DEBUG("At least one unknown did "
			       "underwent a state change.\n");
		read_lock_irqsave(&zfcp_data.config_lock, flags);
		list_for_each_entry(port, &adapter->port_list_head, list) {
			if (!atomic_test_mask((ZFCP_STATUS_PORT_DID_DID
					       | ZFCP_STATUS_PORT_NAMESERVER),
					      &port->status)) {
				ZFCP_LOG_INFO
				    ("Received state change notification."
				     "Trying to open the port with wwpn "
				     "0x%Lx. Hope it's there now.\n",
				     port->wwpn);
				debug_text_event(adapter->erp_dbf, 1,
						 "unsol_els_rscnu:");
				zfcp_erp_port_reopen(port,
						     ZFCP_STATUS_COMMON_ERP_FAILED);
			}
		}
		read_unlock_irqrestore(&zfcp_data.config_lock, flags);
	}
}

static void
zfcp_fsf_incoming_els_plogi(struct zfcp_adapter *adapter,
			    struct fsf_status_read_buffer *status_buffer)
{
	logi *els_logi = (logi *) status_buffer->payload;
	struct zfcp_port *port;
	unsigned long flags;

	zfcp_in_els_dbf_event(adapter, "##plogi", status_buffer, 28);

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	list_for_each_entry(port, &adapter->port_list_head, list) {
		if (port->wwpn == (*(wwn_t *) & els_logi->nport_wwn))
			break;
	}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	if (!port || (port->wwpn != (*(wwn_t *) & els_logi->nport_wwn))) {
		ZFCP_LOG_DEBUG("Re-open port indication received "
			       "for the non-existing port with D_ID "
			       "0x%3.3x, on the adapter "
			       "%s. Ignored.\n",
			       status_buffer->d_id,
			       zfcp_get_busid_by_adapter(adapter));
	} else {
		debug_text_event(adapter->erp_dbf, 1, "unsol_els_plogi:");
		debug_event(adapter->erp_dbf, 1, &els_logi->nport_wwn, 8);
		zfcp_erp_port_forced_reopen(port, 0);
	}
}

static void
zfcp_fsf_incoming_els_logo(struct zfcp_adapter *adapter,
			   struct fsf_status_read_buffer *status_buffer)
{
	struct fcp_logo *els_logo = (struct fcp_logo *) status_buffer->payload;
	struct zfcp_port *port;
	unsigned long flags;

	zfcp_in_els_dbf_event(adapter, "##logo", status_buffer, 16);

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	list_for_each_entry(port, &adapter->port_list_head, list) {
		if (port->wwpn == els_logo->nport_wwpn)
			break;
	}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	if (!port || (port->wwpn != els_logo->nport_wwpn)) {
		ZFCP_LOG_DEBUG("Re-open port indication received "
			       "for the non-existing port with D_ID "
			       "0x%3.3x, on the adapter "
			       "%s. Ignored.\n",
			       status_buffer->d_id,
			       zfcp_get_busid_by_adapter(adapter));
	} else {
		debug_text_event(adapter->erp_dbf, 1, "unsol_els_logo:");
		debug_event(adapter->erp_dbf, 1, &els_logo->nport_wwpn, 8);
		zfcp_erp_port_forced_reopen(port, 0);
	}
}

static void
zfcp_fsf_incoming_els_unknown(struct zfcp_adapter *adapter,
			      struct fsf_status_read_buffer *status_buffer)
{
	zfcp_in_els_dbf_event(adapter, "##undef", status_buffer, 24);
	ZFCP_LOG_NORMAL("warning: Unknown incoming ELS (0x%x) received "
			"for the adapter %s\n",
			*(u32 *) (status_buffer->payload),
			zfcp_get_busid_by_adapter(adapter));

}

void
zfcp_fsf_incoming_els(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_status_read_buffer *status_buffer;
	u32 els_type;
	struct zfcp_adapter *adapter;

	status_buffer = fsf_req->data.status_read.buffer;
	els_type = *(u32 *) (status_buffer->payload);
	adapter = fsf_req->adapter;

	if (els_type == LS_PLOGI)
		zfcp_fsf_incoming_els_plogi(adapter, status_buffer);
	else if (els_type == LS_LOGO)
		zfcp_fsf_incoming_els_logo(adapter, status_buffer);
	else if ((els_type & 0xffff0000) == LS_RSCN)
		/* we are only concerned with the command, not the length */
		zfcp_fsf_incoming_els_rscn(adapter, status_buffer);
	else
		zfcp_fsf_incoming_els_unknown(adapter, status_buffer);
}

/*
 * function:	zfcp_release_nameserver_buffers
 *
 * purpose:	
 *
 * returns:
 */
static void
zfcp_release_nameserver_buffers(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_adapter *adapter = fsf_req->adapter;
	void *buffer = fsf_req->data.send_generic.outbuf;

	/* FIXME: not sure about appeal of this new flag (martin) */
	if (fsf_req->status & ZFCP_STATUS_FSFREQ_POOLBUF)
		mempool_free(buffer, adapter->pool.nameserver);
	else
		kfree(buffer);
}

/*
 * function:	zfcp_get_nameserver_buffers
 *
 * purpose:	
 *
 * returns:
 *
 * locks:       fsf_request_list_lock is held when doing buffer pool 
 *              operations
 */
static int
zfcp_get_nameserver_buffers(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_send_generic *data = &fsf_req->data.send_generic;
	struct zfcp_adapter *adapter = fsf_req->adapter;
	int retval = 0;

	data->outbuf = kmalloc(2 * sizeof (struct fc_ct_iu), GFP_KERNEL);
	if (data->outbuf) {
		memset(data->outbuf, 0, 2 * sizeof (struct fc_ct_iu));
	} else {
		ZFCP_LOG_DEBUG("Out of memory. Could not allocate at "
			       "least one of the buffers "
			       "required for a name-server request on the"
			       "adapter %s directly.. trying emergency pool\n",
			       zfcp_get_busid_by_adapter(adapter));
		data->outbuf =
		    mempool_alloc(adapter->pool.nameserver, GFP_KERNEL);
		if (!data->outbuf) {
			ZFCP_LOG_DEBUG
				("Out of memory. Could not get emergency "
				 "buffer required for a name-server request "
				 "on the adapter %s. All buffers are in "
				 "use.\n",
				 zfcp_get_busid_by_adapter(adapter));
			retval = -ENOMEM;
			goto out;
		}
		memset(data->outbuf, 0, 2 * sizeof (struct fc_ct_iu));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_POOLBUF;
	}
	data->outbuf_length = sizeof (struct fc_ct_iu);
	data->inbuf_length = sizeof (struct fc_ct_iu);
	data->inbuf =
	    (char *) ((unsigned long) data->outbuf + sizeof (struct fc_ct_iu));
 out:
	return retval;
}

/*
 * function:	zfcp_nameserver_request
 *
 * purpose:	
 *
 * returns:
 */
int
zfcp_nameserver_request(struct zfcp_erp_action *erp_action)
{
	int retval = 0;
	struct fc_ct_iu *fc_ct_iu;
	unsigned long lock_flags;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_SEND_GENERIC,
				     &lock_flags,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     &(erp_action->fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of resources. Could not create a "
			      "nameserver registration request for "
			      "adapter %s.\n",
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto failed_req;
	}
	retval = zfcp_get_nameserver_buffers(erp_action->fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of memory. Could not allocate one of "
			      "the buffers required for a nameserver request "
			      "on adapter %s.\n",
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto failed_buffers;
	}

	/* setup name-server request in first page */
	fc_ct_iu =
	    (struct fc_ct_iu *) erp_action->fsf_req->data.send_generic.outbuf;
	fc_ct_iu->revision = ZFCP_CT_REVISION;
	fc_ct_iu->gs_type = ZFCP_CT_DIRECTORY_SERVICE;
	fc_ct_iu->gs_subtype = ZFCP_CT_NAME_SERVER;
	fc_ct_iu->options = ZFCP_CT_SYNCHRONOUS;
	fc_ct_iu->cmd_rsp_code = ZFCP_CT_GID_PN;
	fc_ct_iu->max_res_size = ZFCP_CT_MAX_SIZE;
	fc_ct_iu->data.wwpn = erp_action->port->wwpn;

	erp_action->fsf_req->data.send_generic.handler =
	    zfcp_nameserver_request_handler;
	erp_action->fsf_req->data.send_generic.handler_data =
	    (unsigned long) erp_action->port;
	erp_action->fsf_req->data.send_generic.port =
	    erp_action->adapter->nameserver_port;
	erp_action->fsf_req->erp_action = erp_action;

	/* send this one */
	retval = zfcp_fsf_send_generic(erp_action->fsf_req,
				       ZFCP_NAMESERVER_TIMEOUT,
				       &lock_flags,
				       &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send a"
			      "nameserver request command to adapter %s\n",
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto failed_send;
	}

	goto out;

 failed_send:
	zfcp_release_nameserver_buffers(erp_action->fsf_req);

 failed_buffers:
	zfcp_fsf_req_free(erp_action->fsf_req);
	erp_action->fsf_req = NULL;

 failed_req:
 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/*
 * function:	zfcp_nameserver_request_handler
 *
 * purpose:	
 *
 * returns:
 */
static void
zfcp_nameserver_request_handler(struct zfcp_fsf_req *fsf_req)
{
	struct fc_ct_iu *fc_ct_iu_resp =
	    (struct fc_ct_iu *) (fsf_req->data.send_generic.inbuf);
	struct fc_ct_iu *fc_ct_iu_req =
	    (struct fc_ct_iu *) (fsf_req->data.send_generic.outbuf);
	struct zfcp_port *port =
	    (struct zfcp_port *) fsf_req->data.send_generic.handler_data;

	if (fc_ct_iu_resp->revision != ZFCP_CT_REVISION)
		goto failed;
	if (fc_ct_iu_resp->gs_type != ZFCP_CT_DIRECTORY_SERVICE)
		goto failed;
	if (fc_ct_iu_resp->gs_subtype != ZFCP_CT_NAME_SERVER)
		goto failed;
	if (fc_ct_iu_resp->options != ZFCP_CT_SYNCHRONOUS)
		goto failed;
	if (fc_ct_iu_resp->cmd_rsp_code != ZFCP_CT_ACCEPT) {
		/* FIXME: do we need some specific erp entry points */
		atomic_set_mask(ZFCP_STATUS_PORT_INVALID_WWPN, &port->status);
		goto failed;
	}
	/* paranoia */
	if (fc_ct_iu_req->data.wwpn != port->wwpn) {
		ZFCP_LOG_NORMAL("bug: Port WWPN returned by nameserver lookup "
				"does not correspond to "
				"the expected value on the adapter %s. "
				"(debug info 0x%Lx, 0x%Lx)\n",
				zfcp_get_busid_by_port(port),
				port->wwpn, fc_ct_iu_req->data.wwpn);
		goto failed;
	}

	/* looks like a valid d_id */
	port->d_id = ZFCP_DID_MASK & fc_ct_iu_resp->data.d_id;
	atomic_set_mask(ZFCP_STATUS_PORT_DID_DID, &port->status);
	ZFCP_LOG_DEBUG("busid %s:  WWPN=0x%Lx ---> D_ID=0x%6.6x\n",
		       zfcp_get_busid_by_port(port),
		       port->wwpn, (unsigned int) port->d_id);
	goto out;

 failed:
	ZFCP_LOG_NORMAL("warning: WWPN 0x%Lx not found by nameserver lookup "
			"using the adapter %s\n",
			port->wwpn, zfcp_get_busid_by_port(port));
	ZFCP_LOG_DEBUG("CT IUs do not match:\n");
	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
		      (char *) fc_ct_iu_req, sizeof (struct fc_ct_iu));
	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
		      (char *) fc_ct_iu_resp, sizeof (struct fc_ct_iu));

 out:
	zfcp_release_nameserver_buffers(fsf_req);
}

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
