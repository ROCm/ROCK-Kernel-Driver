/*
 * dvb_i2c.h: simplified i2c interface for DVB adapters to get rid of i2c-core.c
 *
 * Copyright (C) 2002 Holger Waechtler for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/module.h>
#include <asm/semaphore.h>

#include "dvb_i2c.h"
#include "dvb_functions.h"


struct dvb_i2c_device {
	struct list_head list_head;
	struct module *owner;
	int (*attach) (struct dvb_i2c_bus *i2c, void **data);
	void (*detach) (struct dvb_i2c_bus *i2c, void *data);
	void *data;
};

LIST_HEAD(dvb_i2c_buslist);
LIST_HEAD(dvb_i2c_devicelist);

DECLARE_MUTEX(dvb_i2c_mutex);

static int register_i2c_client (struct dvb_i2c_bus *i2c, struct dvb_i2c_device *dev)
{
	struct dvb_i2c_device *client;

	if (!(client = kmalloc (sizeof (struct dvb_i2c_device), GFP_KERNEL)))
		return -ENOMEM;

	client->detach = dev->detach;
	client->owner = dev->owner;

	INIT_LIST_HEAD(&client->list_head);

	list_add_tail (&client->list_head, &i2c->client_list);

	return 0;
}


static void try_attach_device (struct dvb_i2c_bus *i2c, struct dvb_i2c_device *dev)
{
	if (dev->owner) {
		if (!try_module_get(dev->owner))
			return;
	}

	if (dev->attach (i2c, &dev->data) == 0) {
		register_i2c_client (i2c, dev);
	} else {
		if (dev->owner)
			module_put (dev->owner);
	}
}


static void detach_device (struct dvb_i2c_bus *i2c, struct dvb_i2c_device *dev)
{
	dev->detach (i2c, dev->data);

	if (dev->owner)
		module_put (dev->owner);
}


static void unregister_i2c_client_from_bus (struct dvb_i2c_device *dev,
				     struct dvb_i2c_bus *i2c)
{
	struct list_head *entry, *n;

	list_for_each_safe (entry, n, &i2c->client_list) {
                struct dvb_i2c_device *client;

		client = list_entry (entry, struct dvb_i2c_device, list_head);

		if (client->detach == dev->detach) {
			list_del (entry);
			detach_device (i2c, dev);
		}
	}
}


static void unregister_i2c_client_from_all_busses (struct dvb_i2c_device *dev)
{
	struct list_head *entry, *n;

	list_for_each_safe (entry, n, &dvb_i2c_buslist) {
                struct dvb_i2c_bus *i2c;

		i2c = list_entry (entry, struct dvb_i2c_bus, list_head);

		unregister_i2c_client_from_bus (dev, i2c);
	}
}


static void unregister_all_clients_from_bus (struct dvb_i2c_bus *i2c)
{
	struct list_head *entry, *n;

	list_for_each_safe (entry, n, &(i2c->client_list)) {
		struct dvb_i2c_device *dev;

		dev = list_entry (entry, struct dvb_i2c_device, list_head);

		unregister_i2c_client_from_bus (dev, i2c);
	}
}


static void probe_device_on_all_busses (struct dvb_i2c_device *dev)
{
	struct list_head *entry;

	list_for_each (entry, &dvb_i2c_buslist) {
                struct dvb_i2c_bus *i2c;

		i2c = list_entry (entry, struct dvb_i2c_bus, list_head);

		try_attach_device (i2c, dev);
	}
}


static void probe_devices_on_bus (struct dvb_i2c_bus *i2c)
{
	struct list_head *entry;

	list_for_each (entry, &dvb_i2c_devicelist) {
		struct dvb_i2c_device *dev;

		dev = list_entry (entry, struct dvb_i2c_device, list_head);

		try_attach_device (i2c, dev);
	}
}


static struct dvb_i2c_bus* dvb_find_i2c_bus (int (*xfer) (struct dvb_i2c_bus *i2c,
		                                   const struct i2c_msg msgs[],
						   int num),
				      struct dvb_adapter *adapter,
				      int id)
{
	struct list_head *entry;

	list_for_each (entry, &dvb_i2c_buslist) {
		struct dvb_i2c_bus *i2c;

		i2c = list_entry (entry, struct dvb_i2c_bus, list_head);

		if (i2c->xfer == xfer && i2c->adapter == adapter && i2c->id == id)
			return i2c;
	}

	return NULL;
}


struct dvb_i2c_bus*
dvb_register_i2c_bus (int (*xfer) (struct dvb_i2c_bus *i2c,
				   const struct i2c_msg *msgs, int num),
		      void *data, struct dvb_adapter *adapter, int id)
{
	struct dvb_i2c_bus *i2c;

	if (down_interruptible (&dvb_i2c_mutex))
		return NULL;

	if (!(i2c = kmalloc (sizeof (struct dvb_i2c_bus), GFP_KERNEL)))
		return NULL;

	INIT_LIST_HEAD(&i2c->list_head);
	INIT_LIST_HEAD(&i2c->client_list);

	i2c->xfer = xfer;
	i2c->data = data;
	i2c->adapter = adapter;
	i2c->id = id;

	probe_devices_on_bus (i2c);

	list_add_tail (&i2c->list_head, &dvb_i2c_buslist);

	up (&dvb_i2c_mutex);

	return i2c;
}


void dvb_unregister_i2c_bus (int (*xfer) (struct dvb_i2c_bus *i2c,
					  const struct i2c_msg msgs[], int num),
			     struct dvb_adapter *adapter, int id)
{
	struct dvb_i2c_bus *i2c;

	down (&dvb_i2c_mutex);

	if ((i2c = dvb_find_i2c_bus (xfer, adapter, id))) {
		unregister_all_clients_from_bus (i2c);
		list_del (&i2c->list_head);
		kfree (i2c);
	}

	up (&dvb_i2c_mutex);
}


int dvb_register_i2c_device (struct module *owner,
			     int (*attach) (struct dvb_i2c_bus *i2c, void **data),
			     void (*detach) (struct dvb_i2c_bus *i2c, void *data))
{
	struct dvb_i2c_device *entry;

	if (down_interruptible (&dvb_i2c_mutex))
		return -ERESTARTSYS;

	if (!(entry = kmalloc (sizeof (struct dvb_i2c_device), GFP_KERNEL)))
		return -ENOMEM;

	entry->owner = owner;
	entry->attach = attach;
	entry->detach = detach;

	INIT_LIST_HEAD(&entry->list_head);

	probe_device_on_all_busses (entry);

	list_add_tail (&entry->list_head, &dvb_i2c_devicelist);

	up (&dvb_i2c_mutex);

	return 0;
}


int dvb_unregister_i2c_device (int (*attach) (struct dvb_i2c_bus *i2c, void **data))
{
	struct list_head *entry, *n;

	down (&dvb_i2c_mutex);

	list_for_each_safe (entry, n, &dvb_i2c_devicelist) {
		struct dvb_i2c_device *dev;

		dev = list_entry (entry, struct dvb_i2c_device, list_head);

		if (dev->attach == attach) {
			list_del (entry);
			unregister_i2c_client_from_all_busses (dev);
			kfree (entry);
			up (&dvb_i2c_mutex);
			return 0;
                }
        }

	up (&dvb_i2c_mutex);

        return -EINVAL;
}


