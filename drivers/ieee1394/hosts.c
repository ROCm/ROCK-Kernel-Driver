/*
 * IEEE 1394 for Linux
 *
 * Low level (host adapter) management.
 *
 * Copyright (C) 1999 Andreas E. Bombe
 * Copyright (C) 1999 Emanuel Pirker
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>

#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"
#include "nodemgr.h"


static int dummy_transmit_packet(struct hpsb_host *h, struct hpsb_packet *p)
{
        return 0;
}

static int dummy_devctl(struct hpsb_host *h, enum devctl_cmd c, int arg)
{
        return -1;
}

static int dummy_isoctl(struct hpsb_iso *iso, enum isoctl_cmd command, unsigned long arg)
{
	return -1;
}

static struct hpsb_host_driver dummy_driver = {
        .transmit_packet = dummy_transmit_packet,
        .devctl =          dummy_devctl,
	.isoctl =          dummy_isoctl
};

static int alloc_hostnum_cb(struct hpsb_host *host, void *__data)
{
	int *hostnum = __data;

	if (host->id == *hostnum)
		return 1;

	return 0;
}

/**
 * hpsb_alloc_host - allocate a new host controller.
 * @drv: the driver that will manage the host controller
 * @extra: number of extra bytes to allocate for the driver
 *
 * Allocate a &hpsb_host and initialize the general subsystem specific
 * fields.  If the driver needs to store per host data, as drivers
 * usually do, the amount of memory required can be specified by the
 * @extra parameter.  Once allocated, the driver should initialize the
 * driver specific parts, enable the controller and make it available
 * to the general subsystem using hpsb_add_host().
 *
 * Return Value: a pointer to the &hpsb_host if succesful, %NULL if
 * no memory was available.
 */

struct hpsb_host *hpsb_alloc_host(struct hpsb_host_driver *drv, size_t extra,
				  struct device *dev)
{
        struct hpsb_host *h;
	int i;
	int hostnum = 0;

        h = kmalloc(sizeof(struct hpsb_host) + extra, SLAB_KERNEL);
        if (!h) return NULL;
        memset(h, 0, sizeof(struct hpsb_host) + extra);

	h->hostdata = h + 1;
        h->driver = drv;

        INIT_LIST_HEAD(&h->pending_packets);
        spin_lock_init(&h->pending_pkt_lock);

	INIT_LIST_HEAD(&h->addr_space);

	for (i = 0; i < ARRAY_SIZE(h->tpool); i++)
		HPSB_TPOOL_INIT(&h->tpool[i]);

	atomic_set(&h->generation, 0);

	init_timer(&h->timeout);
	h->timeout.data = (unsigned long) h;
	h->timeout.function = abort_timedouts;
	h->timeout_interval = HZ / 20; // 50ms by default

        h->topology_map = h->csr.topology_map + 3;
        h->speed_map = (u8 *)(h->csr.speed_map + 2);

	while (1) {
		if (!nodemgr_for_each_host(&hostnum, alloc_hostnum_cb)) {
			h->id = hostnum;
			break;
		}

		hostnum++;
	}

	memcpy(&h->device, &nodemgr_dev_template_host, sizeof(h->device));
	h->device.parent = dev;
	snprintf(h->device.bus_id, BUS_ID_SIZE, "fw-host%d", h->id);
	device_register(&h->device);

	return h;
}

void hpsb_add_host(struct hpsb_host *host)
{
        highlevel_add_host(host);
        host->driver->devctl(host, RESET_BUS, LONG_RESET);
}

void hpsb_remove_host(struct hpsb_host *host)
{
        host->is_shutdown = 1;
        host->driver = &dummy_driver;

        highlevel_remove_host(host);

	device_unregister(&host->device);
}
