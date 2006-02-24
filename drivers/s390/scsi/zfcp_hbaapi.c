/* 
 * zfcp_hbaapi.c,v 1.4.2.98 2005/02/16 08:51:03 aherrman Exp
 *
 * FCP adapter driver for IBM eServer zSeries
 *
 * interface for HBA API (FC-HBA)
 *
 * (C) Copyright IBM Corp. 2003, 2004
 *
 * Authors:
 *       Andreas Herrmann <aherrman@de.ibm.com>
 *       Stefan Voelkel <Stefan.Voelkel@millenux.com>
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

/*
 * To automatically create the device node (after module loading) use:
 *
 *   minor=`cat /proc/misc | awk "\\$2==\"zfcp_hbaapi\" {print \\$1}"`;
 *   mknod /dev/zfcp_hbaapi c 10 $minor
 */

#define ZFCP_LOG_AREA	ZFCP_LOG_AREA_FC

#define HBAAPI_REVISION "1.4.2.98"

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/stringify.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/ioctl32.h>
#include <linux/ctype.h>

#include <asm/scatterlist.h>
#include <asm/uaccess.h>
#include <asm/current.h>
#include <asm/atomic.h>
#include <asm/semaphore.h>
#include <asm/compat.h>

#include <scsi/scsi_cmnd.h>
#include "zfcp_def.h"
#include "zfcp_ext.h"
#include "zfcp_hbaapi.h"

/* file operations for misc device file */
static int zfcp_hbaapi_open(struct inode *, struct file *);
static ssize_t zfcp_hbaapi_read(struct file *, char __user *, size_t, loff_t *);
static long zfcp_hbaapi_ioctl(struct file *, unsigned int, unsigned long);
#ifdef CONFIG_COMPAT
static long zfcp_hbaapi_compat_ioctl(struct file *, unsigned int, unsigned long);
#endif
static int zfcp_hbaapi_release(struct inode *, struct file *);

/**
 * struct zh_event_item - used to manage an event
 * @event: event itself, as it is passed to userspace
 * @count: reference counter
 * @list: list handling
 *
 * An item in the kernel event queue.
 */
struct zh_event_item {
	struct zh_event event;
	atomic_t count;
	struct list_head list;
};

/**
 * struct zh_client - per client data
 * @sem: 
 * @registered: 1 if the fd is registered for events, else 0
 * @lost: 1 if the fd has lost an event, else 0
 * @next: index for next event in array to be delivered for this client
 * @config: list of private config events
 * @clients: list handling for list of clients
 *
 * This structure is attached to filp->private_data and used throughout the
 * module to save per client data. 
 */
struct zh_client {
	struct semaphore sem;
	unsigned int registered:1;
	unsigned int lost:1;
	unsigned int has_next:1;
	unsigned int next;
	struct list_head config;
	struct list_head clients;
};

/**
 * struct zh_events
 * @lock: spinlock protecting the structure
 * @wq: wait queue to wait for events
 * @registered: number of processes to notify on events
 * @pending: number of events in the queue
 * @queue: array of pointer to event items
 * @start: index in array of first event to be delivered to any client
 * @end: index in array of last event to be delivered to any client
 * @clients: anchor for list of clients
 *
 * This structure contains all data needed for asynchronous event handling
 */
struct zh_shared_events {
	spinlock_t lock;
	wait_queue_head_t wq;
	unsigned short registered;
	unsigned int pending;
	struct zh_event_item **queue;
	unsigned int start, end;
	struct list_head clients;
};

static struct zh_shared_events zh_shared;

/**
 * struct zh_polled_events
 * @lock: spinlock protecting this structure
 * @pending: number of events pending
 * @queue: list of events
 *
 * Polled events must be in an extra queue according to FC-HBA.
 */
struct zh_polled_events {
	spinlock_t lock;
	unsigned short pending;
	struct list_head queue;
};

static struct zh_polled_events zh_polled;

/*
 * module parameters
 */
unsigned int maxshared = ZH_EVENTS_MAX;
unsigned int maxpolled = ZH_EVENTS_MAX;
unsigned int minor = MISC_DYNAMIC_MINOR;

module_param(maxshared, uint, 0);
MODULE_PARM_DESC(maxshared, "Maximum number of events in the shared event"
		" queue, defaults to "__stringify(ZH_EVENTS_MAX));

module_param(maxpolled, uint, 0);
MODULE_PARM_DESC(maxpolled, "Maximum number of events in the polled event"
		" queue, defaults to "__stringify(ZH_EVENTS_MAX));

module_param(minor, uint, 0);
MODULE_PARM_DESC(minor, "Minor of the misc device to register, defaults to"
		"dynamic registration");

/*
 * zfcp_hbaapi_fops - device file operations
 */
static struct file_operations zfcp_hbaapi_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = zfcp_hbaapi_ioctl,
	.open = zfcp_hbaapi_open,
	.release = zfcp_hbaapi_release,
	.read = zfcp_hbaapi_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl = zfcp_hbaapi_compat_ioctl,
#endif
};

/*
 * struct zfcp_hbaapi_misc - description of misc device
 */
static struct miscdevice zfcp_hbaapi_misc = {
	.name = "zfcp_hbaapi",
	.fops = &zfcp_hbaapi_fops
};

/**
 * new_event_item - prepare event structure
 * @flags: kmalloc flags
 * @type: event type
 * Return: container for an event
 * Context: irq/user
 */
static struct zh_event_item*
new_event_item(int gfp_mask, u8 type)
{
	struct zh_event_item *item;

	item = kmalloc(sizeof(*item), gfp_mask);
	if (item) {
		memset(item, 0, sizeof(*item));
		item->event.type = type;
	}
	return item;
}

/**
 * zfcp_search_adapter_port_unit - search for an adapter, port and unit
 * @bus_id: of the adapter to search for
 * @wwpn: of the port to search for, ignored if **port == NULL
 * @lun: of the unit to search for, ignored if **port == NULL || **unit == NULL
 * @adapter: address to write pointer of found adapter structure to
 * @port: address to write pointer of found port structure to,
 *	(iff **port != NULL)
 * @unit: address to write pointer of found unit structure to,
 *	(iff **port != NULL && **unit != NULL)
 * Return: 0 on success, -E* code else
 * Locks: lock/unlock of zfcp_data.config_lock
 *
 * Search for an adapter, port and unit and return their addresses.
 * If @**port == NULL, search only for an adapter.
 * If @**port != NULL, search also for port.
 * If @**port != NULL and @**unit != NULL search also for port and unit.
 */
static int
zfcp_search_adapter_port_unit(char *bus_id, wwn_t wwpn, fcp_lun_t lun,
			      struct zfcp_adapter **adapter,
			      struct zfcp_port **port, struct zfcp_unit **unit)
{
	unsigned long flags;

	if (adapter == NULL)
		return -EINVAL;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	*adapter = zfcp_get_adapter_by_busid(bus_id);
	if (*adapter) {
		int status = atomic_read(&(*adapter)->status);
		if ((status & ZFCP_STATUS_COMMON_ERP_FAILED) ||
		    !(status & ZFCP_STATUS_COMMON_RUNNING) ||
		    !(status & ZFCP_STATUS_COMMON_UNBLOCKED))
			*adapter = NULL;
	}

	if (*adapter == NULL) {
		read_unlock_irqrestore(&zfcp_data.config_lock, flags);
		return -ENOADA;
	}
	zfcp_adapter_get(*adapter);

	if (port) {
		/* WKA ports are ignored by the following routine */
		*port = zfcp_get_port_by_wwpn(*adapter, wwpn);
		if (*port == NULL) {
			zfcp_adapter_put(*adapter);
			read_unlock_irqrestore(&zfcp_data.config_lock, flags);
			return -ENOPOR;
		}
		zfcp_port_get(*port);
		zfcp_adapter_put(*adapter);
	}

	if (unit) {
		*unit = zfcp_get_unit_by_lun(*port, lun);
		if (*unit == NULL) {
			zfcp_port_put(*port);
			read_unlock_irqrestore(&zfcp_data.config_lock, flags);
			return -ENOUNI;
		}
		zfcp_unit_get(*unit);
		zfcp_port_put(*port);
	}

	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
	return 0;
}

/**
 * zfcp_hbaapi_get_unit_config()
 * @filp: address of struct file to add private events to
 * @unit: unit for which configuration is received
 * Locks: called with zfcp_data.config_lock held
 * XXX: ensure that device is valid during this function
 */
static void
zfcp_hbaapi_get_unit_config(struct file *filp, struct zfcp_unit *unit)
{
	struct zh_event_item *item;
	struct zh_client *client;
	struct scsi_device *device;

	BUG_ON(filp == NULL);

	client = (void*) filp->private_data;

	/* ignore units not registered at SCSI mid layer */
	device = unit->device;
	if (unit->device == NULL)
		return;

	item = new_event_item(GFP_ATOMIC, ZH_EVENT_UNIT_ADD);
	if (item == NULL)
		return;

	BUSID_TO_DEVID(zfcp_get_busid_by_unit(unit),
		       &item->event.data.unit_add.devid);
	item->event.data.unit_add.wwpn = unit->port->wwpn;
	item->event.data.unit_add.fclun = unit->fcp_lun;
	item->event.data.unit_add.host = device->host->host_no;
	item->event.data.unit_add.channel = device->channel;
	item->event.data.unit_add.id = device->channel;
	item->event.data.unit_add.lun = device->lun;

	list_add_tail(&item->list, &client->config);
}

/**
 * get_config_units - create unit config events
 * @filp: address of struct file to add private events to
 * @port: port to which the unit belongs
 * Locks: zfcp_data.config_lock must be held
 */
static int
get_config_units(struct file *filp, struct zfcp_port *port)
{
	struct zfcp_unit *unit;
	int ret = 0;
	
	list_for_each_entry(unit, &port->unit_list_head, list)
		if (unit->device) {
			zfcp_hbaapi_get_unit_config(filp, unit);
			ret++;
		}

	return ret;
}

/**
 * zfcp_hbaapi_get_port_config()
 * @filp: address of struct file to add private events to
 * @port: port for which configuration should be received
 * Locks: called with zfcp_data.config_lock held
 */
static void
zfcp_hbaapi_get_port_config(struct file *filp, struct zfcp_port *port)
{
	struct zh_event_item *item;
	struct zh_client *client;

	BUG_ON(filp == NULL);

	client = (void*) filp->private_data;

	item = new_event_item(GFP_ATOMIC, ZH_EVENT_PORT_ADD);
	if (item == NULL)
		return;

	BUSID_TO_DEVID(zfcp_get_busid_by_port(port),
		       &item->event.data.port_add.devid);
	item->event.data.port_add.wwpn = port->wwpn;
	item->event.data.port_add.wwnn = port->wwnn;
	item->event.data.port_add.did = port->d_id;

	list_add_tail(&item->list, &client->config);
}


/**
 * get_config_ports - create port config events or search for port
 * @filp: address of struct file to add private events to
 * @adapter: adapter to which the port belongs
 * @wwpn: generate port add events or search port and call get_config_units()
 * @config_flags: determine which config events are generated
 * Locks: zfcp_data.config_lock must be held
 */
static int
get_config_ports(struct file *filp, struct zfcp_adapter *adapter,
		 wwn_t wwpn, unsigned int config_flags)
{
	struct zfcp_port *port;
	int ret = -ENOPOR, count = 0;

	list_for_each_entry(port, &adapter->port_list_head, list) {
		/* ignore well known ports */
		if (atomic_read(&port->status) & ZFCP_STATUS_PORT_WKA)
			continue;
		if (config_flags == ZH_GET_CONFIG_PORTS) {
			zfcp_hbaapi_get_port_config(filp, port);
			count++;
		} else if (port->wwpn == wwpn) {
			ret = get_config_units(filp, port);
			break;
		}
	}

	if (config_flags == ZH_GET_CONFIG_PORTS)
		ret = count;

	return ret;
}

/**
 * zfcp_hbaapi_get_adapter_config
 * @filp: address of struct file to add private events to
 * @adapter: adapter for which configuration is received
 * Locks: called with zfcp_data.config_sema and zfcp_data.config_lock held
 */
static void
zfcp_hbaapi_get_adapter_config(struct file *filp, struct zfcp_adapter *adapter)
{
	struct zh_event_item *item;
	struct zh_client *client;
	struct Scsi_Host *shost = adapter->scsi_host;

	BUG_ON(filp == NULL);

	client = (void*) filp->private_data;
	
	item = new_event_item(GFP_ATOMIC, ZH_EVENT_ADAPTER_ADD);
	if (item == NULL)
		return;

	BUSID_TO_DEVID(zfcp_get_busid_by_adapter(adapter),
		       &item->event.data.adapter_add.devid);

	if (shost) {
		item->event.data.adapter_add.wwnn = fc_host_node_name(shost);
		item->event.data.adapter_add.wwpn = fc_host_port_name(shost);
	}

	list_add_tail(&item->list, &client->config);
}

/**
 * zfcp_hbaapi_get_config - create adapter config events or search for adapter
 * @filp: address of struct file to add private events to
 * @bus_id: generate adapter add events if 0, or search and get_config_ports()
 * @wwpn: passed to get_config_ports()
 * @config_flags: determine which config events are generated
 * Locks: lock/unlock zfcp_data.config_lock
 *
 * The generated events are private to the passed file descriptor.
 */
static int
zfcp_hbaapi_get_config(struct file *filp, char *bus_id, wwn_t wwpn,
		       unsigned int config_flags)
{
	struct zfcp_adapter *adapter;
	int ret = -ENOADA, count = 0;

	down(&zfcp_data.config_sema);
	read_lock_irq(&zfcp_data.config_lock);
	list_for_each_entry(adapter, &zfcp_data.adapter_list_head, list) {
		if (config_flags == ZH_GET_CONFIG_ADAPTERS) {
			zfcp_hbaapi_get_adapter_config(filp, adapter);
			count++;
		} else if (strncmp(bus_id, zfcp_get_busid_by_adapter(adapter),
				   BUS_ID_SIZE) == 0) {
			ret = get_config_ports(filp, adapter, wwpn,
					       config_flags);
			break;
		}
	}
	read_unlock_irq(&zfcp_data.config_lock);
	up(&zfcp_data.config_sema);

	if (config_flags == ZH_GET_CONFIG_ADAPTERS)
		ret = count;

	return ret;
}

/**
 * encode busid (x.x.xxxx) into u32 value
 */
static inline int
zh_busid_to_u32(char *str, u32 *id)
{
	unsigned long val;
	u32 i;

        if (!isxdigit(str[0]))
		return -EINVAL;
	val = simple_strtoul(str, &str, 16);
	if (val < 0 || val > 0xff || str++[0] != '.')
		return -EINVAL;

	i = (u32) val << 24;

        if (!isxdigit(str[0]))
		return -EINVAL;
	val = simple_strtoul(str, &str, 16);
	if (val < 0 || val > 0xff || str++[0] != '.')
		return -EINVAL;

	i = i | ((u32) val << 16);

        if (!isxdigit(str[0]))
		return -EINVAL;
	val = simple_strtoul(str, &str, 16);
	if (val < 0 || val > 0xffff)
		return -EINVAL;

	*id = i | (u32) val;

	return 0;
}

/**
 * zfcp_hbaapi_get_adapter_attributes - provide data for the API call
 * @devno: of the adapter 
 * @attr: pointer to struct zfcp_adapter_attributes to return attributes
 * Return: 0 on success, -E* else
 * Locks: lock/unlock zfcp_data.config_lock
 */
static int
zfcp_hbaapi_get_adapter_attributes(char *bus_id,
				   struct zfcp_adapter_attributes *attr)
{
	struct zfcp_adapter *adapter;
	struct Scsi_Host *shost;
	int ret = 0;
	u32 id;
	
	memset(attr, 0, sizeof(*attr));

	ret = zfcp_search_adapter_port_unit(bus_id, 0, 0, &adapter, NULL, NULL);
	if (ret)
		return ret;

	down(&zfcp_data.config_sema);
	shost = adapter->scsi_host;
	if (shost) {
		strncpy(attr->serial_number, fc_host_serial_number(shost), 32);
		attr->node_wwn = fc_host_node_name(shost);
	}
	up(&zfcp_data.config_sema);

	strcpy(attr->manufacturer, "IBM");
	switch (adapter->hydra_version) {
	case FSF_ADAPTER_TYPE_FICON:
		strcpy(attr->model, "FICON FCP");
		break;
	case FSF_ADAPTER_TYPE_FICON_EXPRESS:
		strcpy(attr->model, "FICON Express FCP");
		break;
	}
	strcpy(attr->model_description, "zSeries Fibre Channel Adapter");
	sprintf(attr->hardware_version, "0x%x",
		adapter->hardware_version);
	strncpy(attr->driver_version, zfcp_data.driver_version,
		sizeof(attr->driver_version));
	sprintf(attr->firmware_version, "0x%x",
		adapter->fsf_lic_version);
	if ((zh_busid_to_u32(zfcp_get_busid_by_adapter(adapter), &id) == 0))
		attr->vendor_specific_id = id;
	attr->number_of_ports = 1;
	strcpy(attr->driver_name, "zfcp.o");
	/* option_rom_version not used, node_symbolic_name not set */

	zfcp_adapter_put(adapter);
	return ret;
}

/**
 * zfcp_hbaapi_get_port_statistics - get statistics of local port
 * @devno: adapter of which port data should be reported
 * @stat: pointer to struct zfcp_port_statistics to return statistics
 * Return: 0 on success,  -E* else
 * Locks: lock/unlock zfcp_data.config_lock
 */
static int
zfcp_hbaapi_get_port_statistics(char *bus_id,
				struct zfcp_port_statistics *stat)
{
	struct zfcp_adapter *adapter;
	struct fsf_qtcb_bottom_port *data;
	int ret = 0;

	memset(stat, 0, sizeof(*stat));
	
	ret = zfcp_search_adapter_port_unit(bus_id, 0, 0, &adapter, NULL, NULL);
	if (ret)
		return ret;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		zfcp_adapter_put(adapter);
		return -ENOMEM;
	}
	memset(data, 0, sizeof(*data));

	ret = zfcp_fsf_exchange_port_data(NULL, adapter, data);
	if (ret == 0) {
		/* convert fsf_qtcb_bottom_port into
		   zfcp_port_statistics */
		stat->last_reset = data->seconds_since_last_reset;
		stat->tx_frames = data->tx_frames;
		stat->tx_words = data->tx_words;
		stat->rx_frames = data->rx_words;
		stat->lip = data->lip;
		stat->nos = data->nos;
		stat->error_frames = data->error_frames;
		stat->dumped_frames = data->dumped_frames;
		stat->link_failure = data->link_failure;
		stat->loss_of_sync = data->loss_of_sync;
		stat->loss_of_signal = data->loss_of_signal;
		stat->prim_seq_prot_error = data->psp_error_counts;
		stat->invalid_tx_words = data->invalid_tx_words;
		stat->invalid_crc = data->invalid_crcs;
		stat->input_requests = data->input_requests;
		stat->output_requests = data->output_requests;
		stat->control_requests = data->control_requests;
		stat->input_megabytes = data->input_mb;
		stat->output_megabytes = data->output_mb;
	}

	kfree(data);
	zfcp_adapter_put(adapter);
	return ret;
}


/**
 * zfcp_hbaapi_get_port_attributes - get attributes of local port
 * @devno: adapter of which port data should be reported
 * @attr: pointer to struct zfcp_port_attributes to return attributes
 * Return: 0 on success,  -E* else
 * Locks: lock/unlock zfcp_data.config_lock
 */
static int
zfcp_hbaapi_get_port_attributes(char *bus_id,
				struct zfcp_port_attributes *attr)
{
	struct zfcp_adapter *adapter;
	struct Scsi_Host *shost;
	struct zfcp_port *port;
	struct fsf_qtcb_bottom_port *data;
	unsigned long flags;
	int ret = 0;

	memset(attr, 0, sizeof(*attr));
	
	ret = zfcp_search_adapter_port_unit(bus_id, 0, 0, &adapter, NULL, NULL);
	if (ret)
		return ret;

	down(&zfcp_data.config_sema);
	shost = adapter->scsi_host;
	if (shost) {
		attr->wwnn = fc_host_node_name(shost);
		attr->wwpn = fc_host_port_name(shost);
		attr->speed = fc_host_speed(shost);
	}
	up(&zfcp_data.config_sema);

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		zfcp_adapter_put(adapter);
		return -ENOMEM;
	}
	memset(data, 0, sizeof(*data));


	/* ignore well known address ports */
	read_lock_irqsave(&zfcp_data.config_lock, flags);
	attr->discovered_ports = adapter->ports;
	list_for_each_entry(port, &adapter->port_list_head, list) {
		if (atomic_read(&port->status) & ZFCP_STATUS_PORT_WKA)
			if (attr->discovered_ports)
				attr->discovered_ports--;
	}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	ret = zfcp_fsf_exchange_port_data(NULL, adapter, data);
	if (ret == 0) {
		/* convert fsf_qtcb_bottom_port into
		   zfcp_port_attributes */
		attr->fcid = data->fc_port_id;
		attr->type = data->port_type;
		attr->state = data->port_state;
		attr->supported_class_of_service =
			data->class_of_service;
		attr->supported_speed = data->supported_speed;
		attr->max_frame_size = data->maximum_frame_size;
		memcpy(&attr->supported_fc4_types,
		       &data->supported_fc4_types, 32);
		memcpy(&attr->active_fc4_types,
		       &data->active_fc4_types, 32);
	}

	/* fabric_name and symbolic_name not set */

	kfree(data);
	zfcp_adapter_put(adapter);
	return ret;
}

/**
 * zfcp_hbaapi_server_enqueue - check for well known address ports and
 *	eventually enqueue and open a new port with a well known address
 * @adapter: adapter in question
 * @gs_type: type of the generic service to determine the well known address
 */
static struct zfcp_port *
zfcp_hbaapi_server_enqueue(struct zfcp_adapter *adapter, u8 gs_type)
{
	u32 d_id;
	unsigned long flags;
	struct zfcp_port *port;
	int status;

	switch (gs_type) {
	case 0xf7: /* key distribution service */
		d_id = ZFCP_DID_KEY_DISTRIBUTION_SERVICE;
		break;
	case 0xf8: /* alias service */
		d_id = ZFCP_DID_ALIAS_SERVICE;
		break;
	case 0xfa: /* management service */
		d_id = ZFCP_DID_MANAGEMENT_SERVICE;
		break;
	case 0xfb: /* time service */
		d_id = ZFCP_DID_TIME_SERVICE;
		break;
	case 0xfc: /* directory service */
		d_id = ZFCP_DID_DIRECTORY_SERVICE;
		break;
	default:
		ZFCP_LOG_NORMAL("unsupported gs_type: %02X\n", (u32) gs_type);
		return NULL;
	}

	down(&zfcp_data.config_sema);
	read_lock_irqsave(&zfcp_data.config_lock, flags);
	port = zfcp_get_port_by_did(adapter, d_id);
	if (port)
		zfcp_port_get(port);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
	if (!port)
		port = zfcp_port_enqueue(adapter, 0, ZFCP_STATUS_PORT_WKA, d_id);
	up(&zfcp_data.config_sema);

	if (!port) {
		ZFCP_LOG_INFO("error: enqueue of WKA port failed (bus_id=%s, d_id=%X)\n",
			      zfcp_get_busid_by_adapter(adapter), d_id);
		return NULL;
	}
	

	status = atomic_read(&port->status);
	if (!((status & ZFCP_STATUS_PORT_PHYS_OPEN) &&
	      (status & ZFCP_STATUS_COMMON_OPEN))) {
		zfcp_erp_port_reopen(port, 0);
		zfcp_erp_wait(port->adapter);
	}

	status = atomic_read(&port->status);
	if (!((status & ZFCP_STATUS_PORT_PHYS_OPEN) &&
	      (status & ZFCP_STATUS_COMMON_OPEN))) {
		zfcp_port_put(port);
		port = NULL;
	}

	return port;
}

/**
 * zfcp_hbaapi_send_ct_handler() - handler for zfcp_hbaapi_send_ct()
 * @data: a pointer to struct zfcp_send_ct, It was set as handler_data
 *	in zfcp_hbaapi_send_ct().
 * Context: interrupt
 *
 * This handler is called on completion of a send_ct request. We just wake up
 * our own zfcp_hbaapi_send_ct() function.
 */
static void
zfcp_hbaapi_send_ct_handler(unsigned long data)
{
        struct zfcp_send_ct *ct = (struct zfcp_send_ct *) data;
	complete(ct->completion);
}


/**
 * zfcp_hbaapi_send_ct() - send a CT_IU containing FC-GS-4 command
 * @devno: adapter for which port data should be reported
 * @req: scatter-gather list with request data
 * @req_count: number of elements in @req
 * @resp: scatter-gather list for response data
 * @resp_count: number of elements in @resp
 * Return: 0 on success,  -E* else
 * Locks: lock/unlock zfcp_data.config_lock
 *
 * Note: If CT_IU accept response does not fit into provided response buffers,
 *	the request fails and -EIO is returned.
 */
static int
zfcp_hbaapi_send_ct(char *bus_id,
		    struct scatterlist *req, unsigned int req_count,
		    struct scatterlist *resp, unsigned int resp_count)
{
	struct {
		struct zfcp_send_ct ct;
		struct timer_list timer;
	} *loc;
	struct zfcp_adapter *adapter;
	struct ct_hdr *ct_header;
	int ret;

	DECLARE_COMPLETION(wait);

	ret = zfcp_search_adapter_port_unit(bus_id, 0, 0, &adapter, NULL, NULL);
	if (ret)
		return ret;

	loc = kmalloc(sizeof(*loc), GFP_KERNEL);
	if (!loc) {
		zfcp_adapter_put(adapter);
		return -ENOMEM;
	}
	memset(loc, 0, sizeof(*loc));

	ct_header = zfcp_sg_to_address(req);
	loc->ct.port = zfcp_hbaapi_server_enqueue(adapter, ct_header->gs_type);
	zfcp_adapter_put(adapter);

	if (loc->ct.port == NULL) {
		ret = -ENOPOR;
		goto out;
	}

	init_timer(&loc->timer);
	loc->timer.function = zfcp_fsf_request_timeout_handler;
	loc->timer.data = (unsigned long) loc->ct.port->adapter;
	loc->timer.expires = ZFCP_FSF_REQUEST_TIMEOUT;

	loc->ct.timer = &loc->timer;
	loc->ct.req = req;
	loc->ct.resp = resp;
	loc->ct.req_count = req_count;
	loc->ct.resp_count = resp_count;
	loc->ct.handler = zfcp_hbaapi_send_ct_handler;
	loc->ct.handler_data = (unsigned long) &loc->ct;
	loc->ct.completion = &wait;
	loc->ct.timeout = ZFCP_CT_TIMEOUT;

	ret = zfcp_fsf_send_ct(&loc->ct, 0, 0);
	if (ret == 0) {
		wait_for_completion(&wait);
		if (loc->ct.status)
			ret = -EIO;
	}

	del_timer_sync(&loc->timer);
	zfcp_port_put(loc->ct.port);
 out:
	kfree(loc);
	return ret;
}

/**
 * zfcp_hbaapi_ga_nxt_request - send GA_NXT name server request
 * @bus_id: bus_id of adapter
 * @d_id: for which GA_NXT response payload is requested
 * @ct_iu_resp: pointer where to return response payload for GA_NXT request
 */
static int
zfcp_hbaapi_ga_nxt_request(char *bus_id, u32 d_id,
			   struct ct_iu_ga_nxt_resp *ct_iu_resp)
{
	struct {
		struct ct_iu_ga_nxt_req ct_iu_req;
		struct scatterlist req, resp;
	} *loc;
	int ret = 0;

	loc = kmalloc(sizeof(*loc), GFP_KERNEL);
        if (!loc) {
                ret = -ENOMEM;
                goto out;
        }
	memset(loc, 0, sizeof(*loc));

	/* setup nameserver request */
        loc->ct_iu_req.header.revision = ZFCP_CT_REVISION;
        loc->ct_iu_req.header.gs_type = ZFCP_CT_DIRECTORY_SERVICE;
        loc->ct_iu_req.header.gs_subtype = ZFCP_CT_NAME_SERVER;
        loc->ct_iu_req.header.options = ZFCP_CT_SYNCHRONOUS;
        loc->ct_iu_req.header.cmd_rsp_code = ZFCP_CT_GA_NXT;
        loc->ct_iu_req.header.max_res_size = ZFCP_CT_MAX_SIZE;
	loc->ct_iu_req.d_id = ZFCP_DID_MASK & (d_id - 1);
	zfcp_address_to_sg(&loc->ct_iu_req, &loc->req);
	zfcp_address_to_sg(ct_iu_resp, &loc->resp);
	loc->req.length = sizeof(loc->ct_iu_req);
	loc->resp.length = sizeof(*ct_iu_resp);

	ret = zfcp_hbaapi_send_ct(bus_id, &loc->req, 1, &loc->resp, 1);
	if (ret != 0)
		goto failed;

	if (zfcp_check_ct_response(&ct_iu_resp->header))
		ret = -ENOPOR;

 failed:
        kfree(loc);
 out:
	return ret;
}

/**
 * zfcp_hbaapi_gid_pn_request - send GID_PN name server request
 * @bus_id: bus_id of adapter
 * @wwpn: for which GID_PN request is initiated
 * @ct_iu_resp: pointer where to return response payload for GID_PN request
 */
static int
zfcp_hbaapi_gid_pn_request(char *bus_id, wwn_t wwpn,
			   struct ct_iu_gid_pn_resp *ct_iu_resp)
{
        struct {
		struct ct_iu_gid_pn_req ct_iu_req;
		struct scatterlist req, resp;
	} *loc;
	int ret = 0;

	loc = kmalloc(sizeof(*loc), GFP_KERNEL);
        if (!loc) {
                ret = -ENOMEM;
                goto out;
        }
	memset(loc, 0, sizeof(*loc));

	/* setup nameserver request */
        loc->ct_iu_req.header.revision = ZFCP_CT_REVISION;
        loc->ct_iu_req.header.gs_type = ZFCP_CT_DIRECTORY_SERVICE;
        loc->ct_iu_req.header.gs_subtype = ZFCP_CT_NAME_SERVER;
        loc->ct_iu_req.header.options = ZFCP_CT_SYNCHRONOUS;
        loc->ct_iu_req.header.cmd_rsp_code = ZFCP_CT_GID_PN;
        loc->ct_iu_req.header.max_res_size = ZFCP_CT_MAX_SIZE;
	loc->ct_iu_req.wwpn = wwpn;
	zfcp_address_to_sg(&loc->ct_iu_req, &loc->req);
	zfcp_address_to_sg(ct_iu_resp, &loc->resp);
	loc->req.length = sizeof(loc->ct_iu_req);
	loc->resp.length = sizeof(*ct_iu_resp);

	ret = zfcp_hbaapi_send_ct(bus_id, &loc->req, 1, &loc->resp, 1);
	if (ret != 0)
		goto failed;

	if (zfcp_check_ct_response(&ct_iu_resp->header))
		ret = -ENOPOR;

 failed:
        kfree(loc);
 out:
	return ret;
}

/**
 * zfcp_hbaapi_get_discovered_port_attributes - get attributes of a remote port
 * @devno: adapter for which port data should be reported
 * @wwpn: wwn of discovered port
 * @attr: pointer to struct zfcp_port_attributes to return attributes
 * Return: 0 on success,  -E* else
 * Locks: lock/unlock zfcp_data.config_lock
 */
static int
zfcp_hbaapi_get_dport_attributes(char *bus_id, wwn_t wwpn,
				 struct zfcp_port_attributes *attr)
{
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	struct ct_iu_ga_nxt_resp *ct_iu_resp;
	int ret;

	memset(attr, 0, sizeof(*attr));

	ret = zfcp_search_adapter_port_unit(bus_id, wwpn, 0,
					    &adapter, &port, NULL);
	if (ret)
		return ret;

	ct_iu_resp = kmalloc(sizeof(*ct_iu_resp), GFP_KERNEL);
	if (!ct_iu_resp) {
		zfcp_port_put(port);
		return -ENOMEM;
	}

	ret = zfcp_hbaapi_ga_nxt_request(bus_id, port->d_id, ct_iu_resp);
	if (ret == 0) {
		attr->wwnn = port->wwnn;
		attr->wwpn = port->wwpn;
		attr->fabric_name = (wwn_t) ct_iu_resp->fabric_wwn;
		attr->fcid = port->d_id;

		/* map FC-GS-2 port types to HBA API
		   port types */
		switch(ct_iu_resp->port_type) {
		case 0x01: /* N_Port */
			attr->type = FSF_HBA_PORTTYPE_NPORT;
			break;
		case 0x02: /* NL_Port */
			attr->type = FSF_HBA_PORTTYPE_NLPORT;
			break;
		case 0x81: /* F_Port */
			attr->type = FSF_HBA_PORTTYPE_FPORT;
			break;
		case 0x82: /* FL_Port */
			attr->type = FSF_HBA_PORTTYPE_FLPORT;
			break;
		case 0x03: /* F/NL_Port */
		case 0x7f: /* Nx_Port */
		case 0x84: /* E_Port */
			attr->type = FSF_HBA_PORTTYPE_OTHER;
			break;
		case 0x00: /* Unidentified */
		default: /* reserved */
			attr->type = FSF_HBA_PORTTYPE_UNKNOWN;
		}

		attr->state = FSF_HBA_PORTSTATE_UNKNOWN;
		attr->supported_class_of_service = ct_iu_resp->cos;
		memcpy(&attr->active_fc4_types, &ct_iu_resp->fc4_types, 32);
		memcpy(&attr->symbolic_name,
		       &ct_iu_resp->node_symbolic_name,
		       ct_iu_resp->port_symbolic_name_length);
	}
	kfree(ct_iu_resp);

	/* supported_speed, speed, max_frame_size, supported_fc4_types,
	   discovered_ports not set */

	zfcp_port_put(port);
	return ret;
}

/**
 * zfcp_hbaapi_get_did - determine d_id by wwpn or by domain id
 * @bus_id: bus_id of adapter
 * @wwpn: wwpn of port for which d_id should be determined
 * @domain: domain id for which d_id of domain controller should be determined
 * @d_id: d_id to be determined
 *
 * If wwpn is 0 the d_id is determined for the given wwpn otherwise the
 * d_id of the domain controller for given domain is determined.
 */
static int
zfcp_hbaapi_get_did(char *bus_id, wwn_t wwpn, u8 domain, u32 *d_id)
{
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	int ret;

	if (!wwpn) {
		if (domain) {
			*d_id = 0xfffc00 | domain;
			return 0;
		} else 
			return -ENOPOR;
	}

	ret = zfcp_search_adapter_port_unit(bus_id, wwpn, 0,
					    &adapter, &port, NULL);
	if (ret == 0) {
		*d_id = port->d_id;
		zfcp_port_put(port);
		return 0;
	} else if (ret == -ENOPOR) {
		/* do nameserver lookup */
		struct ct_iu_gid_pn_resp *ct_iu_resp;

		ct_iu_resp = kmalloc(sizeof(*ct_iu_resp), GFP_KERNEL);
		if (!ct_iu_resp)
			return -ENOMEM;

		ret = zfcp_hbaapi_gid_pn_request(bus_id, wwpn, ct_iu_resp);
		if (ret) {
			kfree(ct_iu_resp);
			return ret;
		}

		*d_id = ct_iu_resp->d_id;
		kfree(ct_iu_resp);
	}

	return ret;
}

/**
 * add_event_to_polled - add an event to the polled queue
 * @item: container for an event
 */
static void
add_event_to_polled(struct zh_event_item *item)
{
	struct zh_event_item *last;
	unsigned long flags;

	spin_lock_irqsave(&zh_polled.lock, flags);

	if (zh_polled.pending == maxpolled) {
		last = list_entry(zh_polled.queue.next, struct zh_event_item,
				  list);
		list_del(zh_polled.queue.next);
		kfree(last);
	} else
		zh_polled.pending++;

	list_add_tail(&item->list, &zh_polled.queue);

	spin_unlock_irqrestore(&zh_polled.lock, flags);
}

/**
 * add_event_to_shared - add an event to the list of pending events
 * @e: The event that should be added
 * Context: irq/user
 *
 * Events will be thrown away if nobody is registered for delivery. If there
 * are already &maxevents events in the list, the oldest is discarded.
 */
static void
add_event_to_shared(struct zh_event_item *event)
{
	struct zh_event_item *item;
	struct list_head *entry;
	struct zh_client *client;
	unsigned long flags;

	spin_lock_irqsave(&zh_shared.lock, flags);

	if (zh_shared.registered == 0) {
		/* no clients registered for event delivery */
		spin_unlock_irqrestore(&zh_shared.lock, flags);
		kfree(event);
		return;
	}

	if (zh_shared.pending == (unsigned int) maxshared) {
		/* no space left in the array for a new event,
		   (note: maxshared >= 1) */
		list_for_each(entry, &zh_shared.clients) {
			client = list_entry(entry, struct zh_client, clients);
			if (client->has_next &&
			    (client->next == zh_shared.start)) {
				ZFCP_LOG_INFO("lost event for client "
					      "with pid %u\n", current->pid);
				client->lost = 1;
				client->next = (client->next + 1) % maxshared;
			}
		}
		BUG_ON(!zh_shared.pending);
		zh_shared.pending--;
		item = zh_shared.queue[zh_shared.start];
		kfree(item);
		ZFCP_LOG_DEBUG("deleted item at start (%u %u %u)\n",
			       zh_shared.start, zh_shared.end,
			       zh_shared.pending);
		if (zh_shared.pending)
			zh_shared.start = (zh_shared.start + 1) % maxshared;
		ZFCP_LOG_INFO("event queue full, deleted event\n");
	}

	/* determine last event in queue */
	if (zh_shared.pending)
		zh_shared.end = (zh_shared.end + 1) % maxshared;
	zh_shared.pending++;

	/* put event into array */
	atomic_set(&event->count, zh_shared.registered);
	zh_shared.queue[zh_shared.end] = event;
	ZFCP_LOG_DEBUG("added item at end (%u %u %u)\n", zh_shared.start,
		       zh_shared.end, zh_shared.pending);

	list_for_each(entry, &zh_shared.clients) {
		client = list_entry(entry, struct zh_client, clients);
		if (!client->has_next) {
			client->has_next = 1;
			client->next = zh_shared.end;
		}
	}
	spin_unlock_irqrestore(&zh_shared.lock, flags);

	/* wake up all processes waiting for events */
	wake_up_interruptible_all(&zh_shared.wq);
}

/**
 * zfcp_hbaapi_ioc_get_adapterattributes - get attributes of an adapter
 * @u_ptr: user-space pointer to copy data from and to
 * Return: 0 on success, else -E* code
 */
static int noinline
zfcp_hbaapi_ioc_get_adapterattributes(void __user *u_ptr)
{
	struct zh_get_adapterattributes *ioc_data;
	int ret;
	char bus_id[BUS_ID_SIZE] = { 0 };

	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;

	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	DEVID_TO_BUSID(&ioc_data->devid, bus_id);
	ret = zfcp_hbaapi_get_adapter_attributes(bus_id, &ioc_data->attributes);
	if (ret == 0)
		if (copy_to_user(u_ptr, ioc_data, sizeof(*ioc_data)))
			ret = -EFAULT;

 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zh_map_port_speed - maps port speed between FC-FS/FC-GS4 and FC-HBA
 * @speed: address of u32 to be mapped
 * @flag_operating_speed: indicates whether @speed is the operating or the
 *	supported speed
 * Note: For supported speed this works just for the local port, because
 *	there we face the maximum possible port speed. (and not all possible
 *	values for port speed at once.)
 */
static void
zh_map_port_speed(u32 *speed, int flag_operating_speed)
{
	if (flag_operating_speed == ZH_PORT_OPERATING_SPEED) {
		switch(*speed) {
		case 4:
			*speed = 8;
			break;
		case 8:	
			*speed = 4;
			break;
		case (1<<14):
			*speed = 0;
			break;
		}
	} else {		/* ZH_PORT_SUPPORTED_SPEED */
		switch(*speed) {
		case 4:
			*speed = 8;
			break;
		case 8:
			*speed = 4;
			break;
		case (1<<15):
			*speed = 0;
			break;
		}
	}

	return;
}

/**
 * zfcp_hbaapi_ioc_get_portattributes - get attributes of a local port
 * @u_ptr: user-space pointer to copy the data to
 * Return: 0 on success, else -E* code
 */
static int noinline
zfcp_hbaapi_ioc_get_portattributes(void __user *u_ptr)
{
	struct zh_get_portattributes *ioc_data;
	int ret;
	char bus_id[BUS_ID_SIZE] = { 0 };

	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;
	
	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	DEVID_TO_BUSID(&ioc_data->devid, bus_id);
	ret = zfcp_hbaapi_get_port_attributes(bus_id, &ioc_data->attributes);

	if ((ret == 0) || (ret == EOPNOTSUPP)) {
		zh_map_port_speed(&ioc_data->attributes.supported_speed,
				  ZH_PORT_SUPPORTED_SPEED);
		zh_map_port_speed(&ioc_data->attributes.speed,
				  ZH_PORT_OPERATING_SPEED);
		if (copy_to_user(u_ptr, ioc_data, sizeof(*ioc_data)))
			ret = -EFAULT;
	}

 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zfcp_hbaapi_ioc_get_portstatistics - get statistics of a local port
 * @u_ptr: user-space pointer to copy data from and to
 * Return: 0 on success, else -E* code
 */
static int noinline
zfcp_hbaapi_ioc_get_portstatistics(void __user *u_ptr)
{
	struct zh_get_portstatistics *ioc_data;
	int ret;
	char bus_id[BUS_ID_SIZE] = { 0 };

	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;
	
	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	DEVID_TO_BUSID(&ioc_data->devid, bus_id);
	ret = zfcp_hbaapi_get_port_statistics(bus_id, &ioc_data->statistics);

	if ((ret == 0) || (ret == EOPNOTSUPP))
		if (copy_to_user(u_ptr, ioc_data, sizeof(*ioc_data)))
			ret = -EFAULT;

 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zfcp_hbaapi_ioc_get_dportattributes - get attributes of a remote port
 * @u_ptr: user-space pointer to copy data from and to
 * Return: 0 on success, else -E* code
 */
static int noinline
zfcp_hbaapi_ioc_get_dportattributes(void __user *u_ptr)
{
	struct zh_get_portattributes *ioc_data;
	int ret;
	char bus_id[BUS_ID_SIZE] = { 0 };
	
	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;

	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	DEVID_TO_BUSID(&ioc_data->devid, bus_id);
	ret = zfcp_hbaapi_get_dport_attributes(bus_id, ioc_data->wwpn,
					       &ioc_data->attributes);

	if (ret == 0)
		if (copy_to_user(u_ptr, ioc_data, sizeof(*ioc_data)))
			ret = -EFAULT;

 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zfcp_hbaapi_ioc_get_event_buffer - get events from the polled event queue
 * @u_ptr: user-space pointer to copy data from and to
 * Return: number of return events on success, else -E* code
 * 
 * Copy events belonging to an adapter to user-space and delete them.
 */
static int noinline
zfcp_hbaapi_ioc_get_event_buffer(void __user *u_ptr)
{
	int ret;
	struct zh_get_event_buffer *ioc_data;
	struct zh_event_item *item;
	struct list_head *entry, *safe;
	unsigned short i = 0;

	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;
	
	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	if (ioc_data->count > ZH_GET_EVENT_BUFFER_COUNT)
		ioc_data->count = ZH_GET_EVENT_BUFFER_COUNT;

	spin_lock_irq(&zh_polled.lock);

	list_for_each_safe(entry, safe, &zh_polled.queue) {
		item = list_entry(entry, struct zh_event_item, list);

		if (i >= ioc_data->count)
			break;

		if (ioc_data->devid == item->event.data.polled.devid){
			ioc_data->polled[i] = item->event.data.polled;
			list_del(entry);
			kfree(item);
			zh_polled.pending--;
			i++;
		}
	}

	spin_unlock_irq(&zh_polled.lock);

	if (copy_to_user(u_ptr, ioc_data, sizeof(*ioc_data)))
		ret = -EFAULT;
	else
		ret = i;

 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zfcp_hbaapi_ioc_event_start - enable event delivery for a fd
 * @filp: file for which event delivery should be enabled
 * Return: 0 on success, else -E* code
 *
 * Mark the fd as target for events, increase each events
 * "to-be-delivered-to" counter by 1.
 */
static int noinline
zfcp_hbaapi_ioc_event_start(struct file *filp)
{
	struct zh_client *client = (struct zh_client*) filp->private_data;

	if (client->registered)
		return -EINVAL;

	spin_lock_irq(&zh_shared.lock);
	client->registered = 1;
	client->has_next = 0;
	list_add_tail(&client->clients, &zh_shared.clients);
	zh_shared.registered++;
	spin_unlock_irq(&zh_shared.lock);

	return 0;
}

/**
 * zfcp_hbaapi_ioc_event_stop - stop event delivery for a fd
 * @filp: file for which event delivery should be disabled
 * Return: 0 on success, else -E* code
 *
 * Decrease total number of fd's which get events, count down all events
 * _after_ the event delivered to this fd.
 */
static int noinline
zfcp_hbaapi_ioc_event_stop(struct file *filp)
{
	struct zh_client *client = (struct zh_client*) filp->private_data;
	struct zh_event_item *item;

	if (!client->registered)
		return -EINVAL;

	spin_lock_irq(&zh_shared.lock);
	zh_shared.registered--;
	list_del(&client->clients);
	client->registered = 0;
	do {
		/* count down not yet delivered events for this fd */
		if (!client->has_next)
			break;
		item = zh_shared.queue[client->next];
		if (atomic_dec_and_test(&item->count)) {
			BUG_ON((zh_shared.start != client->next) ||
			       !zh_shared.pending);
			zh_shared.pending--;
			zh_shared.queue[zh_shared.start] = NULL;
			ZFCP_LOG_DEBUG("deleted item at start (%u %u %u)\n",
				       zh_shared.start, zh_shared.end,
				       zh_shared.pending);
			if (zh_shared.pending)
				zh_shared.start = (zh_shared.start + 1) %
					maxshared;
			kfree(item);
		}
		if (client->next == zh_shared.end)
			break;
		client->next = (client->next + 1) % maxshared;
	} while (1);
	spin_unlock_irq(&zh_shared.lock);

	return 0;
}

/**
 * zfcp_hbaapi_ioc_event - wait for and receive events
 * @u_ptr: user-space pointer to copy data to
 * @filp: descriptor receiving events
 * Return: 0 on success, -E* code else
 *
 * The heart of the event delivery. Waits for events and delivers the next one
 */
static int noinline
zfcp_hbaapi_ioc_event(void __user *u_ptr, struct file *filp)
{
	struct zh_event_item *item;
	struct zh_client *client = (struct zh_client*) filp->private_data;
	int ret = 0;
	int del_event = 0;

	if (client->registered == 0)
		return -EINVAL;

	if (client->lost) {
		client->lost = 0;
		return -ENXIO;
	}

	/* wait for events */
	ret = wait_event_interruptible(zh_shared.wq, client->has_next);
	if (ret)
		return ret;

	spin_lock_irq(&zh_shared.lock);

	if (client->lost) {
		/* event lost after last check */
		client->lost = 0;
		spin_unlock_irq(&zh_shared.lock);
		return -ENXIO;
	}
	BUG_ON(!client->has_next);

	item = zh_shared.queue[client->next];
	if (atomic_dec_and_test(&item->count)) {
		BUG_ON((zh_shared.start != client->next) || !zh_shared.pending);
		zh_shared.pending--;
		zh_shared.queue[zh_shared.start] = NULL;
		ZFCP_LOG_DEBUG("deleted item at start (%u %u %u)\n",
			       zh_shared.start, zh_shared.end,
			       zh_shared.pending);
		if (zh_shared.pending)
			zh_shared.start = (zh_shared.start + 1) % maxshared;
		del_event = 1;
	}

	if (client->next == zh_shared.end)
		client->has_next = 0;
	else
		client->next = (client->next + 1) % maxshared;

	spin_unlock_irq(&zh_shared.lock);

	if (copy_to_user(u_ptr, &item->event, sizeof(item->event)))
		ret = -EFAULT;

	if (del_event)
		kfree(item);

	return ret;
}

/**
 * zfcp_hbaapi_ioc_event_insert - insert an event into the list
 * @u_ptr: user-space pointer to copy data from
 * Return: 0 on success, else -E* code
 * Debug: DEBUG ONLY
 *
 * Insert a dummy event into the list of events, used to determine if
 * the event handling code is working. Insert a dummy event into the 
 * polled event buffer, used to test the polled event buffer code.
 */
static int noinline
zfcp_hbaapi_ioc_event_insert(void)
{
	struct zh_event_item *item;
	
	item = kmalloc(sizeof(*item), GFP_KERNEL);
	if (!item)
		return -ENOMEM;

	memset(item, 0, sizeof(*item));
	item->event.type = ZH_EVENT_DUMMY;

	add_event_to_shared(item);

	return 0;
}

/**
 * zfcp_hbaapi_alloc_scsi_cmnd - allocate and fill a struct scsi_cmnd
 * @cmd: The SCSI command as specified in the SCSI standards
 * @cmd_size: size of cmd in bytes
 * @sg: scatter gather list for response
 * Return: The created struct scsi_cmnd on success, else NULL
 */
static struct scsi_cmnd *
zfcp_hbaapi_alloc_scsi_cmnd(void *cmd, size_t cmd_size, struct scatterlist *sg,
			    unsigned int sg_count)
{
	struct scsi_cmnd *sc;

	sc = kmalloc(sizeof(*sc), GFP_KERNEL);
	if (!sc)
		return NULL;
	memset(sc, 0, sizeof(*sc));

	/* zfcp uses host_scribble, bh_next and scsi_done, don't touch em */
	sc->sc_data_direction = DMA_FROM_DEVICE;
	sc->use_sg = sg_count;
	sc->sglist_len = sg_count;
	sc->buffer = sg;
	sc->bufflen = zfcp_sg_size(sg, sg_count);
	sc->request_buffer = sc->buffer;
	sc->request_bufflen = sc->bufflen;
	sc->cmd_len = cmd_size;

	memcpy(sc->cmnd, cmd, cmd_size);
	memcpy(sc->data_cmnd, cmd, cmd_size);

	return sc;
}

/** 
 * zfcp_hbaapi_send_scsi - send SCSI command to a unit
 * @devno: devno of the adapter
 * @wwpn: WWPN of the discovered port the unit is attached to
 * @lun: FC LUN of the unit to send the command to
 * @cmnd: address of the prepared struct scsi_cmnd
 * Return: 0 on success, > 0 on SCSI error, -E* code else
 */
static int
zfcp_hbaapi_send_scsi(char *bus_id, wwn_t wwpn, fcp_lun_t lun,
		      struct scsi_cmnd *cmnd)
{
	int ret;
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	struct zfcp_unit *unit;
	struct timer_list *timer;
	
	ret = zfcp_search_adapter_port_unit(bus_id, wwpn, lun,
					    &adapter, &port, &unit);
	if (ret)
		return ret;

	timer = kmalloc(sizeof(struct timer_list), GFP_KERNEL);
	if (!timer) {
		zfcp_unit_put(unit);
		return -ENOMEM;
	}

	init_timer(timer);
	timer->function = zfcp_fsf_request_timeout_handler;
	timer->data = (unsigned long) adapter;
	timer->expires = ZFCP_FSF_REQUEST_TIMEOUT;

	ret = zfcp_scsi_command_sync(unit, cmnd, timer);
	zfcp_unit_put(unit);
	if ((ret != 0) || (host_byte(cmnd->result) == DID_ERROR)) {
		ret = -EIO;
		goto out;
	}

	ret = cmnd->result;

 out:
	del_timer_sync(timer);
	kfree(timer);
	return ret;
}

/**
 * zfcp_hbaapi_do_scsi_command - worker for sending SCSI commands
 * @devid: of the adapter to send via
 * @wwpn: of the port to send the command to
 * @lun: to send the command to
 * @cmd: SCSI command to send
 * @cmd_size: size of the command in bytes
 * @rsp: user-space pointer to copy the response to
 * @rsp_size: size of the user-space buffer
 * @sense: user-space pointer to copy sense data to
 */
static int
zfcp_hbaapi_do_scsi_command(devid_t devid, wwn_t wwpn, fcp_lun_t lun,
			    void *cmd, size_t cmd_size,
			    struct zfcp_sg_list *rsp,
			    void *sense)
{
	struct scsi_cmnd *sc;
	int ret;
	char bus_id[BUS_ID_SIZE] = { 0 };

	sc = zfcp_hbaapi_alloc_scsi_cmnd(cmd, cmd_size, rsp->sg, rsp->count);
	if (sc == NULL)
		return -ENOMEM;

	DEVID_TO_BUSID(&devid, bus_id);
	ret = zfcp_hbaapi_send_scsi(bus_id, wwpn, lun, sc);

	/* the scsi stack sets this, if there was a scsi error */
	if (ret > 0)
		memcpy(sense, sc->sense_buffer, SCSI_SENSE_BUFFERSIZE);

	kfree(sc);

	return ret;
}

/**
 * zfcp_hbaapi_assert_fclun_zero - assert that there is a FC LUN 0
 * @devno: devno of the adapter
 * @wwpn: wwpn of the discovered port
 *
 * Look for an unit at the passed adapter:port with FC LUN 0.
 * Add it if it does not exist. This unit is needed for
 * REPORT_LUNS.
 *
 * Note: No unit with a FC LUN 0 can be added for the same adapter and port
 *	after this call. (We could add an "overwriteable" flag to the
 *	zfcp_unit_t structure as a work-around for this.)
 */
static int
zfcp_hbaapi_assert_fclun_zero(char *bus_id, wwn_t wwpn)
{
	int ret;
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	struct zfcp_unit *unit;

	down(&zfcp_data.config_sema);

	ret = zfcp_search_adapter_port_unit(bus_id, wwpn, 0,
					    &adapter, &port, &unit);
	if (ret != -ENOUNI) {
		if (ret == 0)
			zfcp_unit_put(unit);
		goto out;
	}

	/* unit does not exist yet */
	unit = zfcp_unit_enqueue(port, 0);
	if (unit == NULL) {
		/* unit could not be created */
		ret = -ENOUNI;
		goto out;
	}

	ret = 0;
	/* set flag indicating that LUN 0x0 was created temporarily */
	atomic_set_mask(ZFCP_STATUS_UNIT_TEMPORARY, &unit->status);
	zfcp_erp_unit_reopen(unit, 0);
	zfcp_erp_wait(port->adapter);
	zfcp_unit_put(unit);

 out:
	up(&zfcp_data.config_sema);
	return ret;
}

/**
 * zfcp_hbaapi_release_fclun_zero - remove FC LUN 0 if created for HBA API
 * @devno: devno of the adapter
 * @wwpn: wwpn of the discovered port
 *
 * Look for an unit at the passed adapter:port with FC LUN 0.
 * If the unit was created just for HBA API remove this unit to avoid side effects
 */
static void
zfcp_hbaapi_release_fclun_zero(char *bus_id, wwn_t wwpn)
{
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	struct zfcp_unit *unit;

	down(&zfcp_data.config_sema);

	if (zfcp_search_adapter_port_unit(bus_id, wwpn, 0, &adapter, &port, &unit))
		goto out;

	/* check if LUN 0x0 was ceated for HBA API and remove it */
	if (!atomic_test_mask(ZFCP_STATUS_UNIT_TEMPORARY, &unit->status)) {
		zfcp_unit_put(unit);
		goto out;
	}

	if (atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status)) {
		zfcp_unit_put(unit);
		goto out;
	}

	write_lock_irq(&zfcp_data.config_lock);
	if (unit && (atomic_read(&unit->refcount) == 1)) {
		atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);
		list_move(&unit->list, &port->unit_remove_lh);
	} else {
		zfcp_unit_put(unit);
		unit = NULL;
	}
	write_unlock_irq(&zfcp_data.config_lock);

	if (!unit)
		goto out;

	zfcp_erp_unit_shutdown(unit, 0);
	zfcp_erp_wait(unit->port->adapter);
	zfcp_unit_put(unit);
	zfcp_unit_dequeue(unit);

 out:
	up(&zfcp_data.config_sema);
}

/**
 * zfcp_hbaapi_report_luns_helper - send SCSI command REPORT LUNS
 * @u_ptr: user-space pointer to copy data from and to
 * Return: 0 on success, < 0 -E* code, > 0 SCSI error
 */
static int
zfcp_hbaapi_report_luns_helper(struct zh_scsi_report_luns *ioc_data)
{
	int ret;
	struct zfcp_sg_list sg_list;
	struct scsi_report_luns_cmd cmd = { 0 };
	char bus_id[BUS_ID_SIZE] = { 0 };

	if (ioc_data->rsp_buffer_size < SCSI_REPORT_LUNS_SIZE_MIN)
		return -EINVAL;

	DEVID_TO_BUSID(&ioc_data->devid, bus_id);
	ret = zfcp_hbaapi_assert_fclun_zero(bus_id, ioc_data->wwpn);
	if (ret)
		return ret;

	ret = zfcp_sg_list_alloc(&sg_list, ioc_data->rsp_buffer_size);
	if (ret < 0)
		return ret;
	
	cmd.op = REPORT_LUNS;
	cmd.alloc_length = ioc_data->rsp_buffer_size;

	ret = zfcp_hbaapi_do_scsi_command(ioc_data->devid, ioc_data->wwpn, 0,
					  &cmd, sizeof(cmd), &sg_list,
					  &ioc_data->sense);
	if (ret >= 0)
		ret = zfcp_sg_list_copy_to_user(ioc_data->rsp_buffer,
						&sg_list,
						ioc_data->rsp_buffer_size);

	zfcp_hbaapi_release_fclun_zero(bus_id, ioc_data->wwpn);

	zfcp_sg_list_free(&sg_list);

	return ret;
}

/**
 * zfcp_hbaapi_ioc_scsi_report_luns - send SCSI command REPORT LUNS
 * @u_ptr: user-space pointer to copy data from and to
 * Return: 0 on success, < 0 -E* code, > 0 SCSI error
 */
static int noinline
zfcp_hbaapi_ioc_scsi_report_luns(void __user *u_ptr)
{
	int ret;
	struct zh_scsi_report_luns *ioc_data;

	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;

	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	ret = zfcp_hbaapi_report_luns_helper(ioc_data);

	if (ret >= 0)
		if (copy_to_user(u_ptr, ioc_data, sizeof(*ioc_data)))
			ret = -EFAULT;

 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zfcp_hbaapi_ioc_scsi_read_capacity - send SCSI command READ CAPACITY
 * @u_ptr: user-space pointer to copy data from and to
 * Return: 0 on success, < 0 -E* code, > 0 SCSI error
 */
static int noinline
zfcp_hbaapi_ioc_scsi_read_capacity(void __user *u_ptr)
{
	int ret;
	struct zfcp_sg_list sg_list;
	struct zh_scsi_read_capacity *ioc_data;
	struct scsi_read_capacity_cmd cmd = { 0 };

	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;

	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	ret = zfcp_sg_list_alloc(&sg_list, ZH_SCSI_READ_CAPACITY_SIZE);
	if (ret < 0)
		goto out;

	cmd.op = READ_CAPACITY;

	ret = zfcp_hbaapi_do_scsi_command(ioc_data->devid, ioc_data->wwpn,
					  ioc_data->fclun, &cmd, sizeof(cmd),
					  &sg_list, ioc_data->sense);

	if (ret >= 0) {
		memcpy(ioc_data->read_capacity,
		       zfcp_sg_to_address(&sg_list.sg[0]),
		       sg_list.sg[0].length);

		if (copy_to_user(u_ptr, ioc_data, sizeof(*ioc_data)))
			ret = -EFAULT;
	}

	zfcp_sg_list_free(&sg_list);

 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zfcp_hbaapi_ioc_scsi_inquiry - send SCSI command INQUIRY
 * @u_ptr: user-space pointer to copy data from and to
 * Return: 0 on success, < 0 -E* code, > 0 SCSI error
 */
static int noinline
zfcp_hbaapi_ioc_scsi_inquiry(void __user *u_ptr)
{
	int ret;
	struct zfcp_sg_list sg_list;
	struct zh_scsi_inquiry *ioc_data;
	struct scsi_inquiry_cmd cmd = { 0 };

	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;
	
	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	ret = zfcp_sg_list_alloc(&sg_list, ZH_SCSI_INQUIRY_SIZE);
	if (ret < 0)
		goto out;

	cmd.op  = INQUIRY;
	cmd.alloc_length = 255;

	if (ioc_data->evpd)	{
		cmd.evpd = 1;
		cmd.page_code = ioc_data->page_code;
	}

	ret = zfcp_hbaapi_do_scsi_command(ioc_data->devid, ioc_data->wwpn,
					  ioc_data->fclun, &cmd, sizeof(cmd),
					  &sg_list, ioc_data->sense);

	if (ret >= 0) {
		memcpy(ioc_data->inquiry, zfcp_sg_to_address(&sg_list.sg[0]),
		       sg_list.sg[0].length);

		if (copy_to_user(u_ptr, ioc_data, sizeof(*ioc_data)))
			ret = -EFAULT;
	}

	zfcp_sg_list_free(&sg_list);

 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zfcp_hbaapi_ioc_clear_config - remove pending config events
 * @filp: fd requesting to clear its config
 * Return: always 0
 */
static int noinline
zfcp_hbaapi_ioc_clear_config(struct file *filp)
{
	struct zh_event_item *item;
	struct list_head *entry, *safe;
	struct zh_client *client = (struct zh_client*) filp->private_data;

	list_for_each_safe(entry, safe, &client->config) {
		item = list_entry(entry, struct zh_event_item, list);
		list_del(entry);
		kfree(item);
	}

	INIT_LIST_HEAD(&client->config);

	return 0;
}

/**
 * zfcp_hbaapi_ioc_get_config - create config events
 * @u_ptr: user-space pointer to copy data from
 * @filp: requesting fd
 * Return: no of created events, else -E* code
 *
 * With this ioctl events are generated and attached to the fd only.  Used to
 * enumerate currently configured adapters/ports/units. Subsequent calls
 * discard prior created events.
 */
static int noinline
zfcp_hbaapi_ioc_get_config(void __user *u_ptr, struct file *filp)
{
	struct zh_get_config *ioc_data;
	struct zh_client *head = (void*) filp->private_data;
	char bus_id[BUS_ID_SIZE] = { 0 };
	int ret;

	if (!list_empty(&head->config))
		zfcp_hbaapi_ioc_clear_config(filp);
	
	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;
	
	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	DEVID_TO_BUSID(&ioc_data->devid, bus_id);
	ret = zfcp_hbaapi_get_config(filp, bus_id, ioc_data->wwpn,
				     ioc_data->flags);

 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zfcp_hbaapi_ioc_get_rnid - get RNID payload of the adapter
 * @u_ptr: user-space pointer to copy data from and to
 *
 * Note: We set data in zfcp_hbaapi since we can not access the data
 *	the adapter sends out.
 * Note: wwpn and wwnn not set because not used in ZFCP HBA API Library
 */
static int noinline
zfcp_hbaapi_ioc_get_rnid(void __user *u_ptr)
{
	struct zh_get_rnid *ioc_data;
	int ret = 0;

	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;
	
	/* XXX just copy devid from user space */
	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	memset(&ioc_data->payload, 0, sizeof(ioc_data->payload));
	ioc_data->payload.code = ZFCP_LS_RNID;
	ioc_data->payload.node_id_format = 0xDF;
	ioc_data->payload.common_id_length =
		sizeof(struct zfcp_ls_rnid_common_id);
	ioc_data->payload.specific_id_length =
		sizeof(struct zfcp_ls_rnid_general_topology_id);

	/* all other fields not set */
	ioc_data->payload.specific_id.associated_type = 0x000000a;
	ioc_data->payload.specific_id.physical_port_number = 1;
	ioc_data->payload.specific_id.nr_attached_nodes = 1;

	if (copy_to_user(u_ptr, ioc_data, sizeof(*ioc_data)))
		ret = -EFAULT;

 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zfcp_hbaapi_send_els_handler() - handler for zfcp_hbaapi_send_els()
 * @data: a pointer to struct zfcp_send_els, It was set as handler_data
 *	in zfcp_hbaapi_send_els().
 * Context: interrupt
 *
 * This handler is called on completion of a send_els request. We just wake up
 * our own zfcp_hbaapi_send_els() function.
 */
static void
zfcp_hbaapi_send_els_handler(unsigned long data)
{
        struct zfcp_send_els *els = (struct zfcp_send_els *) data;
	complete(els->completion);
}

/**
 * zfcp_hbaapi_send_els - send an ELS to a port
 * @bus_id: bus_id of adapter device
 * @d_id: destination id of target port
 * @send: scatterlist describing the ELS payload to be sent
 * @send_count: number of elements in the send scatterlist
 * @receive: scatterlist describing buffers for the reply payload
 * @receive_count: number of elements in the receive scatterlist
 * Return: 0 on success, -E* code else
 * Locks: lock/unlock of zfcp_data.config_lock
 */
static int
zfcp_hbaapi_send_els(char *bus_id, u32 d_id,struct scatterlist *send,
		     unsigned int send_count, struct scatterlist *receive,
		     unsigned int receive_count)
{
	int ret;
	struct {
		struct zfcp_send_els els;
		struct timer_list timer;
	} *loc;
	struct zfcp_adapter *adapter;

	DECLARE_COMPLETION(wait);

	ret = zfcp_search_adapter_port_unit(bus_id, 0, 0,
					    &adapter, NULL, NULL);
	if (ret)
  		return ret;

	loc = kmalloc(sizeof(*loc), GFP_KERNEL);
	if (!loc) {
		zfcp_adapter_put(adapter);
		return -ENOMEM;
	}
	memset(loc, 0, sizeof(*loc));

	init_timer(&loc->timer);
	loc->timer.function = zfcp_fsf_request_timeout_handler;
	loc->timer.data = (unsigned long) adapter;
	loc->timer.expires = ZFCP_FSF_REQUEST_TIMEOUT;

	loc->els.timer = &loc->timer;
	loc->els.d_id = d_id;
	loc->els.adapter = adapter;
	loc->els.req = send;
	loc->els.req_count = send_count;
	loc->els.resp = receive;
	loc->els.resp_count = receive_count;
	loc->els.handler = zfcp_hbaapi_send_els_handler;
	loc->els.handler_data = (unsigned long) &loc->els;
	loc->els.completion = &wait;

	ret = zfcp_fsf_send_els(&loc->els);
	if (ret == 0) {
		wait_for_completion(&wait);
		if (loc->els.status)
			ret = (loc->els.status == -EREMOTEIO) ?
				-EREMOTEIO : -EIO;
	}

	del_timer_sync(&loc->timer);
	kfree(loc);
	zfcp_adapter_put(adapter);

	return ret;
}

/**
 * zfcp_hbaapi_ioc_send_rnid - send ELS command RNID
 * @u_ptr: user-space pointer to copy data from and to
 *
 * Send a FC-FS ELS RNID to a discovered port.
 */
static int noinline
zfcp_hbaapi_ioc_send_rnid(void __user *u_ptr)
{
	int ret;
	struct zfcp_sg_list req, resp;
	struct zh_send_rnid *ioc_data;
	struct zfcp_ls_rnid *rnid;
	char bus_id[BUS_ID_SIZE] = { 0 };
	u32 d_id;

	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;
	
	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	ret = zfcp_sg_list_alloc(&req, sizeof(struct zfcp_ls_rnid));
	if (ret < 0)
		goto out;

	ret = zfcp_sg_list_alloc(&resp, sizeof(struct zfcp_ls_rnid_acc));
	if (ret < 0)
		goto free_req;

	rnid = zfcp_sg_to_address(req.sg);
	rnid->code = ZFCP_LS_RNID;
	rnid->node_id_format = 0xDF;

	DEVID_TO_BUSID(&ioc_data->devid, bus_id);
	ret = zfcp_hbaapi_get_did(bus_id, ioc_data->wwpn, 0, &d_id);
	if (ret)
		goto free_resp;

	ret = zfcp_hbaapi_send_els(bus_id, d_id, req.sg, req.count,
				   resp.sg, resp.count);
	if ((ret == 0) || (ret == -EREMOTEIO)) {
		ioc_data->size = resp.sg->length;
		memcpy(&ioc_data->payload, zfcp_sg_to_address(resp.sg),
		       ioc_data->size);
		
		if (copy_to_user(u_ptr, ioc_data, sizeof(*ioc_data)))
			ret = -EFAULT;
	}

 free_resp:
	zfcp_sg_list_free(&resp);
 free_req:
	zfcp_sg_list_free(&req);
 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zfcp_hbaapi_ioc_send_rls - send ELS command RLS
 * @u_ptr: user-space pointer to copy data from and to
 *
 * Send a FC-FS ELS RLS to a discovered port.
 */
static int noinline
zfcp_hbaapi_ioc_send_rls(void __user *u_ptr)
{
	int ret;
	struct zfcp_sg_list req, resp;
	struct zh_send_rls *ioc_data;
	struct zfcp_ls_rls *rls;
	char bus_id[BUS_ID_SIZE] = { 0 };
	u32 d_id;

	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;
	
	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	ret = zfcp_sg_list_alloc(&req, sizeof(struct zfcp_ls_rls));
	if (ret < 0)
		return ret;

	ret = zfcp_sg_list_alloc(&resp, sizeof(struct zfcp_ls_rls_acc));
	if (ret < 0)
		goto free_req;

	rls = zfcp_sg_to_address(&req.sg[0]);
	memset(rls, 0, sizeof(struct zfcp_ls_rls));
	rls->code = ZFCP_LS_RLS;

	DEVID_TO_BUSID(&ioc_data->devid, bus_id);
	ret = zfcp_hbaapi_get_did(bus_id, ioc_data->wwpn, 0, &d_id);
	if (ret)
		goto free_resp;
		
	rls->n_port_id = d_id;

	ret = zfcp_hbaapi_send_els(bus_id, d_id, req.sg, req.count,
				   resp.sg, resp.count);
	if ((ret == 0) || (ret == -EREMOTEIO)) {
		ioc_data->size = resp.sg[0].length;
		memcpy(&ioc_data->payload, zfcp_sg_to_address(&resp.sg[0]),
		       ioc_data->size);
		
		if (copy_to_user(u_ptr, ioc_data, sizeof(*ioc_data)))
			ret = -EFAULT;
	}

 free_resp:
	zfcp_sg_list_free(&resp);
 free_req:
	zfcp_sg_list_free(&req);
 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zfcp_hbaapi_ioc_send_rps - send ELS command RPS
 * @u_ptr: user-space pointer to copy data from and to
 *
 * Send a FC-FS ELS RPS to a port (domain controller or discovered port).
 */
static int noinline
zfcp_hbaapi_ioc_send_rps(void __user *u_ptr)
{
	int ret;
	struct zfcp_sg_list req, resp;
	struct zh_send_rps *ioc_data;
	struct zfcp_ls_rps *rps;
	char bus_id[BUS_ID_SIZE] = { 0 };
	u32 d_id;

	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;
	
	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	ret = zfcp_sg_list_alloc(&req, sizeof(struct zfcp_ls_rps));
	if (ret < 0)
		return ret;

	ret = zfcp_sg_list_alloc(&resp, sizeof(struct zfcp_ls_rps_acc));
	if (ret < 0)
		goto free_req;

	rps = zfcp_sg_to_address(&req.sg[0]);
	memset(rps, 0, sizeof(struct zfcp_ls_rps));
	rps->code = ZFCP_LS_RPS;

	DEVID_TO_BUSID(&ioc_data->devid, bus_id);
	ret = zfcp_hbaapi_get_did(bus_id, ioc_data->agent_wwpn,
				  ioc_data->domain, &d_id);
	if (ret)
		goto free_resp;
		
	if (ioc_data->object_wwpn) {
		rps->flag = 0x02;
		rps->port_selection = ioc_data->object_wwpn;
	} else {
		rps->flag = 0x01;
		/* we are big-endian that's why this works */
		rps->port_selection = ioc_data->port_number;
	}

	ret = zfcp_hbaapi_send_els(bus_id, d_id, req.sg, req.count,
				   resp.sg, resp.count);
	if ((ret == 0) || (ret == -EREMOTEIO)) {
		ioc_data->size = resp.sg[0].length;
		memcpy(&ioc_data->payload, zfcp_sg_to_address(&resp.sg[0]),
		       ioc_data->size);
		
		if (copy_to_user(u_ptr, ioc_data, sizeof(*ioc_data)))
			ret = -EFAULT;
	}

 free_resp:
	zfcp_sg_list_free(&resp);
 free_req:
	zfcp_sg_list_free(&req);
 out:
	kfree(ioc_data);
	return ret;
}

/**
 * zfcp_hbaapi_send_ct_helper - send a Generic Service command
 * @send_ct: user-space pointer to parameter structure
 * Return: 0 on success, -E* code else
 *
 * Send a FC-GS CT IU
 */
static int
zfcp_hbaapi_send_ct_helper(struct zh_send_ct *send_ct)
{
	int ret;
	struct zfcp_sg_list req, resp;
	char bus_id[BUS_ID_SIZE] = { 0 };

	if ((send_ct->req_length < sizeof(struct ct_hdr)) ||
	    (send_ct->resp_length < sizeof(struct ct_hdr)))
		return -EINVAL;

	ret = zfcp_sg_list_alloc(&req, send_ct->req_length);
	if (ret < 0)
		return ret;

	if (zfcp_sg_list_copy_from_user(&req, send_ct->req,
					send_ct->req_length)) {
		ret = -EFAULT;
		goto free_req;
	}

	ret = zfcp_sg_list_alloc(&resp, send_ct->resp_length);
	if (ret < 0) {
		goto free_req;
	}

	DEVID_TO_BUSID(&send_ct->devid, bus_id);
	ret = zfcp_hbaapi_send_ct(bus_id, req.sg, req.count, resp.sg,
				  resp.count);
	if (ret == 0) {
		ret = zfcp_sg_list_copy_to_user(send_ct->resp, &resp,
						send_ct->resp_length);
		zfcp_check_ct_response(zfcp_sg_to_address(resp.sg));
	}

	zfcp_sg_list_free(&resp);
 free_req:
	zfcp_sg_list_free(&req);
	
	return ret;
}

/**
 * zfcp_hbaapi_ioc_send_ct - send a Generic Service command
 * @u_ptr: user-space pointer to parameter structure
 * Return: 0 on success, -E* code else
 *
 * Send a FC-GS CT IU
 */
static int noinline
zfcp_hbaapi_ioc_send_ct(void __user *u_ptr)
{
	int ret;
	struct zh_send_ct *ioc_data;

	ioc_data = kmalloc(sizeof(*ioc_data), GFP_KERNEL);
	if (!ioc_data)
		return -ENOMEM;
	
	if (copy_from_user(ioc_data, u_ptr, sizeof(*ioc_data))) {
		ret = -EFAULT;
		goto out;
	}

	ret = zfcp_hbaapi_send_ct_helper(ioc_data);

 out:
	kfree(ioc_data);
	return ret;
}

#ifdef CONFIG_COMPAT
/**
 * struct zh_send_ct32 - data needed to send out a Generic Service command,
 *	32BIT version
 * @devid: id of HBA via which to send CT
 * @req_length: size the request buffer
 * @req: request buffer
 * @resp_length: size of response buffer
 * @resp: response buffer
 */
struct zh_send_ct32
{
	devid_t devid;
	u32 req_length;
	u32 req;
	u32 resp_length;
	u32 resp;
} __attribute__((packed));

#define ZH_IOC_SEND_CT32 _IOWR(ZH_IOC_MAGIC, 7, struct zh_send_ct32)

/**
 * zfcp_hbaapi_send_ct32 - ioctl32 conversion function for ZH_IOC_SEND_CT
 * @cmd: command to execute
 * @arg: parameter(s) for the command
 * Return:  0 on success, else -E* code
 */
static int
zfcp_hbaapi_send_ct32(unsigned int cmd, unsigned long arg)
{
	struct {
		struct zh_send_ct ioc_data;
		struct zh_send_ct32 ioc_data32;
	} *loc;
	struct zh_send_ct32 __user *u_ptr;
	int ret;

	u_ptr = compat_ptr((compat_uptr_t) arg);

	loc = kmalloc(sizeof(*loc), GFP_KERNEL);
	if (!loc)
		return -ENOMEM;
	
	if (copy_from_user(&loc->ioc_data32, u_ptr, sizeof(loc->ioc_data32))) {
		ret = -EFAULT;
		goto out;
	}

	loc->ioc_data.devid = loc->ioc_data32.devid;
	loc->ioc_data.req_length = loc->ioc_data32.req_length;
	loc->ioc_data.resp_length = loc->ioc_data32.resp_length;
	loc->ioc_data.req = compat_ptr(loc->ioc_data32.req);
	loc->ioc_data.resp = compat_ptr(loc->ioc_data32.resp);

	ret = zfcp_hbaapi_send_ct_helper(&loc->ioc_data);

 out:
	kfree(loc);
	return ret;
}

/**
 * struct zh_scsi_report_luns32 - data needed for an REPORT_LUNS, 32BIT version
 * @devid: of the adapter
 * @wwpn: of the port
 * @rsp_buffer: pointer to response buffer
 * @rsp_buffer_size: of the response buffer
 * @sense: buffer for sense data
 */
struct zh_scsi_report_luns32
{
	devid_t devid;
	wwn_t wwpn;
	u32 rsp_buffer;
	u32 rsp_buffer_size;
	u8 sense[ZH_SCSI_SENSE_BUFFERSIZE];
} __attribute__((packed));

#define ZH_IOC_SCSI_REPORT_LUNS32 \
_IOW(ZH_IOC_MAGIC, 10, struct zh_scsi_report_luns32)

/**
 * zfcp_hbaapi_scsi_report_luns32 - ioctl32 conversion function for
 *	ZH_SCSI_REPORT_LUNS32
 * @cmd: command to execute
 * @arg: parameter(s) for the command
 * Return:  0 on success, else -E* code
 */
static int
zfcp_hbaapi_scsi_report_luns32(unsigned int cmd, unsigned long arg)
{
	struct {
		struct zh_scsi_report_luns ioc_data;
		struct zh_scsi_report_luns32 ioc_data32;
	} *loc;
	struct zh_scsi_report_luns32 __user *u_ptr;
	int ret;

	u_ptr = compat_ptr((compat_uptr_t) arg);

	loc = kmalloc(sizeof(*loc), GFP_KERNEL);
	if (!loc)
		return -ENOMEM;
	
	if (copy_from_user(&loc->ioc_data32, u_ptr, sizeof(loc->ioc_data32))) {
		ret = -EFAULT;
		goto out;
	}

	loc->ioc_data.devid = loc->ioc_data32.devid;
	loc->ioc_data.wwpn = loc->ioc_data32.wwpn;
	loc->ioc_data.rsp_buffer = compat_ptr(loc->ioc_data32.rsp_buffer);
	loc->ioc_data.rsp_buffer_size = loc->ioc_data32.rsp_buffer_size;

	ret = zfcp_hbaapi_report_luns_helper(&loc->ioc_data);

	if (ret >= 0) {
		memcpy(&loc->ioc_data32.sense, &loc->ioc_data.sense,
		       ZH_SCSI_SENSE_BUFFERSIZE);
		if (copy_to_user(u_ptr, &loc->ioc_data32,
				 sizeof(loc->ioc_data32)))
			ret = -EFAULT;
		else
			ret = 0;
	}

 out:
	kfree(loc);
	return ret;
}

/**
 * zfcp_hbaapi_compat_ioctl - compat ioctl method of misc device
 * @filp: struct file
 * @cmd: ioctl request
 * @arg: parameter(s) for the command
 * Return:  0 on success, else -E* code
 * 
 * This is the main interaction method between the ZFCP HBA API
 * library and the kernel. Here we only determine what we should do,
 * and then call the corresponding worker method.
 */
static long
zfcp_hbaapi_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret;
	struct zh_client *client = (void*) filp->private_data;
	void __user *argp = compat_ptr(arg);

	ret = -ENOIOCTLCMD;

	switch (cmd) {
	case ZH_IOC_GET_ADAPTERATTRIBUTES:
	case ZH_IOC_GET_PORTATTRIBUTES:
	case ZH_IOC_GET_PORTSTATISTICS:
	case ZH_IOC_GET_DPORTATTRIBUTES:
	case ZH_IOC_GET_RNID:
	case ZH_IOC_SEND_RNID:
	case ZH_IOC_SCSI_INQUIRY:
	case ZH_IOC_SCSI_READ_CAPACITY:
	case ZH_IOC_GET_EVENT_BUFFER:
	case ZH_IOC_GET_CONFIG:
	case ZH_IOC_CLEAR_CONFIG:
	case ZH_IOC_EVENT_START:
	case ZH_IOC_EVENT_STOP:
	case ZH_IOC_EVENT:
	case ZH_IOC_EVENT_INSERT:
	case ZH_IOC_SEND_RLS:
	case ZH_IOC_SEND_RPS:
		ret = zfcp_hbaapi_ioctl(filp, cmd, (unsigned long) argp);
		break;
	case ZH_IOC_SCSI_REPORT_LUNS32:
		if (down_interruptible(&client->sem))
			return -ERESTARTSYS;
		ret = zfcp_hbaapi_scsi_report_luns32(cmd, arg);
		up(&client->sem);
		break;
	case ZH_IOC_SEND_CT32:
		if (down_interruptible(&client->sem))
			return -ERESTARTSYS;
		ret = zfcp_hbaapi_send_ct32(cmd, arg);
		up(&client->sem);
		break;
	}

	return ret;
}
#endif

/**
 * zfcp_hbaapi_open - open method of misc device
 * @inode: struct inode
 * @filp: struct file
 * Return: 0 on success, else -ENOMEM or -ENODEV
 * 
 * Called when the zfcp_hbaapi device file is opened. Initializes
 * filp->private_data.
 *
 * Note:
 * Access is serialized with a semaphore. Cloned file descriptors may block.
 * E.g. fd = open(); fork(); parent:ioctl(ZH_IOC_EVENT); child:read();
 * The child will be block until the parent returns from the ioctl(), _if_
 * they use _exactly the same_ file descriptor.
 * Different file descriptors do _not_ block each other.
 */
static int
zfcp_hbaapi_open(struct inode *inode, struct file *filp)
{
	struct zh_client *data;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	memset(data, 0, sizeof(*data));

	sema_init(&data->sem, 1);
	INIT_LIST_HEAD(&data->config);
	INIT_LIST_HEAD(&data->clients);
	filp->private_data = data;

	return 0;
}

/**
 * zfcp_hbaapi_release - release method of misc device
 * @inode: struct inode
 * @filp: struct file
 * Return: always 0
 *
 * Called when all copies of a file descriptor are closed, thus we
 * can mess around with private_data, and free it.
 */
static int
zfcp_hbaapi_release(struct inode *inode, struct file *filp)
{
	zfcp_hbaapi_ioc_event_stop(filp);
	zfcp_hbaapi_ioc_clear_config(filp);

	kfree(filp->private_data);
	filp->private_data = NULL;

	return 0;
}

/**
 * zfcp_hbaapi_read - read method of misc device
 * @filp: file pointer of opened device file
 * @buf: buffer to return data
 * @count: maximum number of bytes to be returned
 * @off: file offset
 *
 * Used to read the whole configuration data, e.g. adapters, ports or units
 * from zfcp.
 */
static ssize_t
zfcp_hbaapi_read(struct file *filp, char __user * buf, size_t count,
		 loff_t *off)
{
	size_t i, ret;
	struct zh_event_item *item;
	struct list_head *entry, *safe;
	struct zh_client *client = (struct zh_client*) filp->private_data;

	if (down_interruptible(&client->sem))
		return -ERESTARTSYS;

	if (count < sizeof(item->event)) {
		ret = -ENOSPC;
		goto up;
	}

	if (list_empty(&client->config)) {
		ret = 0;
		goto up;
	}

	count /= sizeof(item->event);
	i = 0;

	list_for_each_safe(entry, safe, &client->config)
	{
		item = list_entry(entry, struct zh_event_item, list);
		
		if (copy_to_user(buf, &item->event, sizeof(item->event))) {
			ret = -EFAULT;
			goto up;
		}
		
		list_del(entry);
		kfree(item);

		buf += sizeof(item->event);

		if (++i >= count)
			break;
	}

	ret = i * sizeof(item->event);

 up:
	up(&client->sem);

	return ret;
};

/**
 * zfcp_hbaapi_ioctl - ioctl method of misc device
 * @filp: struct file
 * @cmd: ioctl request
 * @arg: parameter(s) for the command
 * Return:  0 on success, else -E* code
 * 
 * This is the main interaction method between the ZFCP HBA API
 * library and the kernel. Here we only determine what we should do,
 * and then call the corresponding worker method.
 */
static long
zfcp_hbaapi_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{

	long ret;
	struct zh_client *client = (void*) filp->private_data;

	if (down_interruptible(&client->sem))
		return -ERESTARTSYS;

	switch (cmd) {
	case ZH_IOC_GET_ADAPTERATTRIBUTES:
		ret = zfcp_hbaapi_ioc_get_adapterattributes((void __user *)arg);
		break;
	case ZH_IOC_GET_PORTATTRIBUTES:
		ret = zfcp_hbaapi_ioc_get_portattributes((void __user *) arg);
		break;
	case ZH_IOC_GET_DPORTATTRIBUTES:
		ret = zfcp_hbaapi_ioc_get_dportattributes((void __user *) arg);
		break;
	case ZH_IOC_GET_PORTSTATISTICS:
		ret = zfcp_hbaapi_ioc_get_portstatistics((void __user *) arg);
		break;
	case ZH_IOC_GET_EVENT_BUFFER:
		ret = zfcp_hbaapi_ioc_get_event_buffer((void __user *) arg);
		break;
	case ZH_IOC_EVENT_START:
		ret = zfcp_hbaapi_ioc_event_start(filp);
		break;
	case ZH_IOC_EVENT_STOP:
		ret = zfcp_hbaapi_ioc_event_stop(filp);
		break;
	case ZH_IOC_EVENT:
		ret = zfcp_hbaapi_ioc_event((void __user *) arg, filp);
		break;
	case ZH_IOC_EVENT_INSERT: /* DEBUG ONLY */
		ret = zfcp_hbaapi_ioc_event_insert();
		break;
	case ZH_IOC_SCSI_INQUIRY:
		ret = zfcp_hbaapi_ioc_scsi_inquiry((void __user *) arg);
		break;
	case ZH_IOC_SCSI_READ_CAPACITY:
		ret = zfcp_hbaapi_ioc_scsi_read_capacity((void __user *) arg);
		break;
	case ZH_IOC_SCSI_REPORT_LUNS:
		ret = zfcp_hbaapi_ioc_scsi_report_luns((void __user *)
			(struct zh_scsi_report_luns *) arg);
		break;
	case ZH_IOC_GET_CONFIG:
		ret = zfcp_hbaapi_ioc_get_config((void __user *) arg, filp);
		break;
	case ZH_IOC_CLEAR_CONFIG:
		ret = zfcp_hbaapi_ioc_clear_config(filp);
		break;
	case ZH_IOC_GET_RNID:
		ret = zfcp_hbaapi_ioc_get_rnid((void __user *) arg);
		break;
	case ZH_IOC_SEND_RNID:
		ret = zfcp_hbaapi_ioc_send_rnid((void __user *) arg);
		break;
	case ZH_IOC_SEND_RLS:
		ret = zfcp_hbaapi_ioc_send_rls((void __user *) arg);
		break;
	case ZH_IOC_SEND_RPS:
		ret = zfcp_hbaapi_ioc_send_rps((void __user *) arg);
		break;
	case ZH_IOC_SEND_CT:
		ret = zfcp_hbaapi_ioc_send_ct((void __user *) arg);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	up(&client->sem);

	return ret;
}

/**
 * zfcp_hbaapi_cb_adapter_add - callback for adapter add events
 * @adapter: adapter which was added to zfcp
 * Context: irq
 * Locks: lock/unlock of zfcp_data.config_lock
 * Note: no configuration changes while this function is executed.
 */
static void
zfcp_hbaapi_cb_adapter_add(struct zfcp_adapter *adapter)
{
	struct zh_event_item *item;
	struct Scsi_Host *shost;
	
	item = new_event_item(GFP_ATOMIC, ZH_EVENT_ADAPTER_ADD);
	if (item == NULL)
		return;

	BUSID_TO_DEVID(zfcp_get_busid_by_adapter(adapter),
		       &item->event.data.adapter_add.devid);

	
	read_lock(&zfcp_data.config_lock);
	shost = adapter->scsi_host;
	if (shost) {
		item->event.data.adapter_add.wwnn = fc_host_node_name(shost);
		item->event.data.adapter_add.wwpn = fc_host_port_name(shost);
	}
	read_unlock(&zfcp_data.config_lock);

	add_event_to_shared(item);
}

/**
 * zfcp_hbaapi_cb_port_add() - called on port add events
 * @port: which was added to zfcp
 * Context: irq
 */
static void
zfcp_hbaapi_cb_port_add(struct zfcp_port *port)
{
	struct zh_event_item *item;

	item = new_event_item(GFP_ATOMIC, ZH_EVENT_PORT_ADD);
	if (item == NULL)
		return;

	BUSID_TO_DEVID(zfcp_get_busid_by_port(port),
		       &item->event.data.port_add.devid);
	item->event.data.port_add.wwpn = port->wwpn;
	item->event.data.port_add.wwnn = port->wwnn;
	item->event.data.port_add.did = port->d_id;

	add_event_to_shared(item);
}

/**
 * zfcp_hbaapi_cb_unit_add() - callback for unit add events
 * @unit: unit which was added to zfcp
 * Context: irq
 * Note: no configuration changes while this function is executed.
 */
static void
zfcp_hbaapi_cb_unit_add(struct zfcp_unit *unit)
{
	struct zh_event_item *item;

	/* ignore units not registered at SCSI mid layer */
	if (unit->device == NULL)
		return;

	item = new_event_item(GFP_ATOMIC, ZH_EVENT_UNIT_ADD);
	if (item == NULL)
		return;

	BUSID_TO_DEVID(zfcp_get_busid_by_unit(unit),
		       &item->event.data.unit_add.devid);
	item->event.data.unit_add.wwpn = unit->port->wwpn;
	item->event.data.unit_add.fclun = unit->fcp_lun;
	item->event.data.unit_add.host =
		unit->port->adapter->scsi_host->host_no;
	item->event.data.unit_add.channel = unit->device->channel;
	item->event.data.unit_add.id = unit->device->channel;
	item->event.data.unit_add.lun = unit->device->lun;

	add_event_to_shared(item);
}

/**
 * zfcp_hbaapi_incoming_rscn - auxiliary function for incoming RSCN ELS
 * @bus_id: bus_id of adapter where RSCN is detected
 * @s_id: initiator of RSCN
 * @payload: RSCN payload
 * Context: irq
 */
static void
zfcp_hbaapi_incoming_rscn(char *bus_id, u32 s_id,
			  struct fcp_rscn_head *payload)
{
	int nr_entries, i;
	struct zh_event_item *item;
	struct fcp_rscn_element *rscn_element;

	nr_entries = payload->payload_len / sizeof(struct fcp_rscn_head);

	/* skip head */
	rscn_element = (struct fcp_rscn_element *) (payload + 1);

	for (i = 0; i < nr_entries; i++, rscn_element++) {
		
		item = new_event_item(GFP_ATOMIC, ZH_EVENT_POLLED);
		if (item == NULL)
			return;

		item->event.data.polled.event = ZH_EVENT_POLLED_RSCN;
		BUSID_TO_DEVID(bus_id, &item->event.data.polled.devid);
		item->event.data.polled.data.rscn.port_fc_id = s_id;
		memcpy(&item->event.data.polled.data.rscn.port_page,
		       rscn_element, sizeof(*rscn_element));
		
		add_event_to_polled(item);
	}
}

/**
 * zfcp_hbaapi_cb_incoming_els - callback for incoming ELS's
 * @adapter: adapter where incoming ELS is detected
 * @payload: ELS payload
 * Context: irq
 * Note: no configuration changes while this function is executed.
 */
static void
zfcp_hbaapi_cb_incoming_els(struct zfcp_adapter *adapter, void *payload)
{
	u8 *code = payload;
	u32 s_id = 0;
	struct Scsi_Host *shost;

	if (*code != ZFCP_LS_RSCN)
		return;

	read_lock(&zfcp_data.config_lock);
	shost = adapter->scsi_host;
	if (shost)
		s_id = fc_host_port_id(shost);
	read_unlock(&zfcp_data.config_lock);

	if (s_id)
		zfcp_hbaapi_incoming_rscn(zfcp_get_busid_by_adapter(adapter),
					  s_id, payload);
}

/**
 * zfcp_hbaapi_cb_link_down - callback for link down events
 * @adapter: adapter where link down is detected
 * Context: irq
 * Note: no configuration changes while this function is executed.
 *
 * Currently this callback is used only if the local link is down.
 */
static void
zfcp_hbaapi_cb_link_down(struct zfcp_adapter *adapter)
{
	struct zh_event_item *item;
	u32 s_id = 0;
	struct Scsi_Host *shost;

	item = new_event_item(GFP_ATOMIC, ZH_EVENT_POLLED);
	if (item == NULL)
		return;

	item->event.data.polled.event = ZH_EVENT_POLLED_LINK_DOWN;
	BUSID_TO_DEVID(zfcp_get_busid_by_adapter(adapter),
		       &item->event.data.polled.devid);

	read_lock(&zfcp_data.config_lock);
	shost = adapter->scsi_host;
	if (shost)
		s_id = fc_host_port_id(shost);
	read_unlock(&zfcp_data.config_lock);

	if (s_id) {
		item->event.data.polled.data.link.port_fc_id = s_id;
		add_event_to_polled(item);
	}
}

/**
 * zfcp_hbaapi_cb_link_up - callback for link up events
 * @adapter: adapter where link up is detected
 * Context: irq
 * Note: no configuration changes while this function is executed.
 *
 * Currently this callback is used only if the local link is up.
 */
static void
zfcp_hbaapi_cb_link_up(struct zfcp_adapter *adapter)
{
	struct zh_event_item *item;
	u32 s_id = 0;
	struct Scsi_Host *shost;

	item = new_event_item(GFP_ATOMIC, ZH_EVENT_POLLED);
	if (item == NULL)
		return;

	item->event.data.polled.event = ZH_EVENT_POLLED_LINK_UP;
	BUSID_TO_DEVID(zfcp_get_busid_by_adapter(adapter),
		       &item->event.data.polled.devid);

	read_lock(&zfcp_data.config_lock);
	shost = adapter->scsi_host;
	if (shost)
		s_id = fc_host_port_id(shost);
	read_unlock(&zfcp_data.config_lock);

	if (s_id) {
		item->event.data.polled.data.link.port_fc_id = s_id;
		add_event_to_polled(item);
	}
}

static struct zfcp_callbacks zfcp_hbaapi_cb = {
	.incoming_els = zfcp_hbaapi_cb_incoming_els,
	.link_down = zfcp_hbaapi_cb_link_down,
	.link_up = zfcp_hbaapi_cb_link_up,
	.adapter_add = zfcp_hbaapi_cb_adapter_add,
	.port_add = zfcp_hbaapi_cb_port_add,
	.unit_add = zfcp_hbaapi_cb_unit_add
};

/**
 * zfcp_hbaapi_init - module initialization
 * Return: 0 on success, else < 0
 * 
 * Sets owner, registers with zfcp, registers misc device, initializes
 * global events structure.
 */
static int
zfcp_hbaapi_init(void)
{
	int ret = 0;
	
	if (maxshared <= 0) {
		ZFCP_LOG_NORMAL("illegal value for maxshared: %d, "
				"minimum is 1\n", maxshared);
		return -EINVAL;
	}

	zh_shared.queue = kmalloc(sizeof(void*) * maxshared, GFP_KERNEL);
	if (!zh_shared.queue)
		return -ENOMEM;
	memset(zh_shared.queue, 0, sizeof(void*) * maxshared);

	/* register a misc char device */
	zfcp_hbaapi_misc.minor = minor;
	ret = misc_register(&zfcp_hbaapi_misc);
	if (ret < 0)
		goto failed;

	/* initialize shared events */
	spin_lock_init(&zh_shared.lock);
	init_waitqueue_head(&zh_shared.wq);
	zh_shared.registered = 0;
	zh_shared.pending = 0;
	INIT_LIST_HEAD(&zh_shared.clients);

	/* initalize polled events */
	spin_lock_init(&zh_polled.lock);
	zh_polled.pending = 0;
	INIT_LIST_HEAD(&zh_polled.queue);

	zfcp_register_callbacks(&zfcp_hbaapi_cb);

	ZFCP_LOG_NORMAL("loaded hbaapi.o, version %s, maxshared=%d, "
			"maxpolled=%d\n", HBAAPI_REVISION, maxshared,
			maxpolled);

	if (minor == MISC_DYNAMIC_MINOR)
		ZFCP_LOG_NORMAL("registered dynamic minor with misc device\n");
	else
		ZFCP_LOG_NORMAL("registered minor %d with misc device\n",
				minor);
	goto out;

 failed:
	kfree(zh_shared.queue);
 out:
	return ret;
}

/**
 * zfcp_hbaapi_exit - module finalization
 */
static void
zfcp_hbaapi_exit(void)
{
	struct list_head *entry, *save;
	struct zh_event_item *item;
	unsigned int i;

	zfcp_unregister_callbacks();

	misc_deregister(&zfcp_hbaapi_misc);

	if (zh_shared.pending) {
		ZFCP_LOG_NORMAL("error: event queue not empty while unloading "
				"module\n");
		/* free any leftover items in event array */
		for (i=0; i<maxshared; i++)
			if (zh_shared.queue[i]) {
				kfree(zh_shared.queue);
				zh_shared.pending--;
			}
		if (zh_shared.pending)
			ZFCP_LOG_DEBUG("number of pending events: %u\n",
					zh_shared.pending);
	}
	kfree(zh_shared.queue);

	/* throw away polled events */
	list_for_each_safe(entry, save, &zh_polled.queue) {
		item = list_entry(entry, struct zh_event_item, list);
		list_del(entry);
		kfree(item);
	}
}

module_init(zfcp_hbaapi_init);
module_exit(zfcp_hbaapi_exit);

#undef ZFCP_LOG_AREA
