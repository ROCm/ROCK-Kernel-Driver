/*****************************************************************************
Copyright(c) 2004 - 2005 Intel Corporation. All rights reserved.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2 of the License, or (at your option)
any later version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59
Temple Place - Suite 330, Boston, MA  02111-1307, USA.

The full GNU General Public License is included in this distribution in the
file called LICENSE.
*****************************************************************************/
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/hardirq.h>
#include <linux/spinlock.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>

static spinlock_t dma_list_lock;
static LIST_HEAD(dma_device_list);
static LIST_HEAD(dma_client_list);

/* --- sysfs implementation --- */

static ssize_t show_memcpy_count(struct class_device *cd, char *buf)
{
	struct dma_chan *chan = container_of(cd, struct dma_chan, class_dev);

	sprintf(buf, "%lu\n", chan->memcpy_count);
	return strlen(buf) + 1;
}

static ssize_t show_bytes_transferred(struct class_device *cd, char *buf)
{
	struct dma_chan *chan = container_of(cd, struct dma_chan, class_dev);

	sprintf(buf, "%lu\n", chan->bytes_transferred);
	return strlen(buf) + 1;
}

static ssize_t show_in_use(struct class_device *cd, char *buf)
{
	struct dma_chan *chan = container_of(cd, struct dma_chan, class_dev);

	sprintf(buf, "%d\n", (chan->client ? 1 : 0));
	return strlen(buf) + 1;
}

static struct class_device_attribute dma_class_attrs[] = {
	__ATTR(memcpy_count, S_IRUGO, show_memcpy_count, NULL),
	__ATTR(bytes_transferred, S_IRUGO, show_bytes_transferred, NULL),
	__ATTR(in_use, S_IRUGO, show_in_use, NULL),
	__ATTR_NULL
};

static void dma_async_device_cleanup(struct kref *kref);

static void dma_class_dev_release(struct class_device *cd)
{
	struct dma_chan *chan = container_of(cd, struct dma_chan, class_dev);
	kref_put(&chan->device->refcount, dma_async_device_cleanup);
}

static struct class dma_devclass = {
	.name            = "dma",
	.class_dev_attrs = dma_class_attrs,
	.release = dma_class_dev_release,
};

/* --- client and device registration --- */

/**
 * dma_client_chan_alloc - try to allocate a channel to a client
 * @client: &dma_client
 *
 * Called with dma_list_lock held.
 */
static struct dma_chan * dma_client_chan_alloc(struct dma_client *client)
{
	struct dma_device *device;
	struct dma_chan *chan;

	/* Find a channel, any DMA engine will do */
	list_for_each_entry(device, &dma_device_list, global_node) {
		list_for_each_entry(chan, &device->channels, device_node) {
			if (chan->client)
				continue;

			if (chan->device->device_alloc_chan_resources(chan) >= 0) {
				kref_get(&device->refcount);
				kref_init(&chan->refcount);
				INIT_RCU_HEAD(&chan->rcu);
				chan->client = client;
				list_add_tail(&chan->client_node, &client->channels);
				return chan;
			}
		}
	}

	return NULL;
}

/**
 * dma_client_chan_free - release a DMA channel
 * @chan: &dma_chan
 */
void dma_async_device_cleanup(struct kref *kref);
void dma_chan_cleanup(struct kref *kref)
{
	struct dma_chan *chan = container_of(kref, struct dma_chan, refcount);
	chan->device->device_free_chan_resources(chan);
	chan->client = NULL;
	kref_put(&chan->device->refcount, dma_async_device_cleanup);
}

static void dma_chan_free_rcu(struct rcu_head *rcu) {
	struct dma_chan *chan = container_of(rcu, struct dma_chan, rcu);
	kref_put(&chan->refcount, dma_chan_cleanup);
}

static void dma_client_chan_free(struct dma_chan *chan)
{
	call_rcu(&chan->rcu, dma_chan_free_rcu);
}

/**
 * dma_chans_rebalance - reallocate channels to clients
 *
 * When the number of DMA channel in the system changes,
 * channels need to be rebalanced among clients
 */
static void dma_chans_rebalance(void)
{
	struct dma_client *client;
	struct dma_chan *chan;

	spin_lock(&dma_list_lock);
	list_for_each_entry(client, &dma_client_list, global_node) {

		while (client->chans_desired > client->chan_count) {
			chan = dma_client_chan_alloc(client);
			if (!chan)
				break;

			client->chan_count++;
			client->event_callback(client, chan, DMA_RESOURCE_ADDED);
		}

		while (client->chans_desired < client->chan_count) {
			chan = list_entry(client->channels.next, struct dma_chan, client_node);
			list_del(&chan->client_node);
			client->chan_count--;
			client->event_callback(client, chan, DMA_RESOURCE_REMOVED);
			dma_client_chan_free(chan);
		}
	}
	spin_unlock(&dma_list_lock);
}

/**
 * dma_async_client_register - allocate and register a &dma_client
 * @event_callback: callback for notification of channel addition/removal
 */
struct dma_client * dma_async_client_register(dma_event_callback event_callback)
{
	struct dma_client *client;

	client = kmalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return NULL;

	INIT_LIST_HEAD(&client->channels);

	client->chans_desired = 0;
	client->chan_count = 0;
	client->event_callback = event_callback;

	spin_lock(&dma_list_lock);
	list_add_tail(&client->global_node, &dma_client_list);
	spin_unlock(&dma_list_lock);

	return client;
}

/**
 * dma_async_client_unregister - unregister a client and free the &dma_client
 * @client:
 *
 * Force frees any allocated DMA channels, frees the &dma_client memory
 */
void dma_async_client_unregister(struct dma_client *client)
{
	struct dma_chan *chan, *_chan;

	if (!client)
		return;

	list_for_each_entry_safe(chan, _chan, &client->channels, client_node) {
		dma_client_chan_free(chan);
	}

	spin_lock(&dma_list_lock);
	list_del(&client->global_node);
	spin_unlock(&dma_list_lock);

	kfree(client);
	dma_chans_rebalance();
}

/**
 * dma_async_client_chan_request - request DMA channels
 * @client: &dma_client
 * @number: count of DMA channels requested
 *
 * Clients call dma_async_client_chan_request() to specify how many
 * DMA channels they need, 0 to free all currently allocated.
 * The resulting allocations/frees are indicated to the client via the
 * event callback.
 */
void dma_async_client_chan_request(struct dma_client *client,
			unsigned int number)
{
	client->chans_desired = number;
	dma_chans_rebalance();
}

/**
 * dma_async_device_register -
 * @device: &dma_device
 */
int dma_async_device_register(struct dma_device *device)
{
	static int id;
	int chancnt = 0;
	struct dma_chan* chan;

	if (!device)
		return -ENODEV;

	init_completion(&device->done);
	kref_init(&device->refcount);
	device->dev_id = id++;

	/* represent channels in sysfs. Probably want devs too */
	list_for_each_entry(chan, &device->channels, device_node) {
		chan->chan_id = chancnt++;
		chan->class_dev.class = &dma_devclass;
		chan->class_dev.dev = NULL;
		snprintf(chan->class_dev.class_id, BUS_ID_SIZE, "dma%dchan%d",
		         device->dev_id, chan->chan_id);

		kref_get(&device->refcount);
		class_device_register(&chan->class_dev);
	}

	spin_lock(&dma_list_lock);
	list_add_tail(&device->global_node, &dma_device_list);
	spin_unlock(&dma_list_lock);

	dma_chans_rebalance();

	return 0;
}

/**
 * dma_async_device_unregister -
 * @device: &dma_device
 */
static void dma_async_device_cleanup(struct kref *kref) {
	struct dma_device *device = container_of(kref, struct dma_device, refcount);
	complete(&device->done);
}

void dma_async_device_unregister(struct dma_device* device)
{
	struct dma_chan *chan;

	list_for_each_entry(chan, &device->channels, device_node) {
		if (chan->client) {
			list_del(&chan->client_node);
			chan->client->chan_count--;
			chan->client->event_callback(chan->client, chan, DMA_RESOURCE_REMOVED);
			dma_client_chan_free(chan);
		}
		class_device_unregister(&chan->class_dev);
	}

	spin_lock(&dma_list_lock);
	list_del(&device->global_node);
	spin_unlock(&dma_list_lock);

	dma_chans_rebalance();

	kref_put(&device->refcount, dma_async_device_cleanup);
	wait_for_completion(&device->done);
}

static int __init dma_bus_init(void)
{
	spin_lock_init(&dma_list_lock);

	return class_register(&dma_devclass);
}

subsys_initcall(dma_bus_init);

EXPORT_SYMBOL(dma_async_client_register);
EXPORT_SYMBOL(dma_async_client_unregister);
EXPORT_SYMBOL(dma_async_client_chan_request);
EXPORT_SYMBOL(dma_async_memcpy_buf_to_buf);
EXPORT_SYMBOL(dma_async_memcpy_buf_to_pg);
EXPORT_SYMBOL(dma_async_memcpy_pg_to_pg);
EXPORT_SYMBOL(dma_async_memcpy_complete);
EXPORT_SYMBOL(dma_async_memcpy_issue_pending);
EXPORT_SYMBOL(dma_async_device_register);
EXPORT_SYMBOL(dma_async_device_unregister);
