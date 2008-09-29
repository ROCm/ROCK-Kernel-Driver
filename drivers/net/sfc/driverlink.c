/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005      Fen Systems Ltd.
 * Copyright 2005-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include "net_driver.h"
#include "efx.h"
#include "driverlink.h"

/* Protects @efx_driverlink_lock and @efx_driver_list */
static DEFINE_MUTEX(efx_driverlink_lock);

/* List of all registered drivers */
static LIST_HEAD(efx_driver_list);

/* List of all registered Efx ports */
static LIST_HEAD(efx_port_list);

/**
 * Driver link handle used internally to track devices
 * @efx_dev: driverlink device handle exported to consumers
 * @efx: efx_nic backing the driverlink device
 * @port_node: per-device list head
 * @driver_node: per-driver list head
 */
struct efx_dl_handle {
	struct efx_dl_device efx_dev;
	struct efx_nic *efx;
	struct list_head port_node;
	struct list_head driver_node;
};

static struct efx_dl_handle *efx_dl_handle(struct efx_dl_device *efx_dev)
{
	return container_of(efx_dev, struct efx_dl_handle, efx_dev);
}

/* Remove an Efx device, and call the driver's remove() callback if
 * present. The caller must hold @efx_driverlink_lock. */
static void efx_dl_del_device(struct efx_dl_device *efx_dev)
{
	struct efx_dl_handle *efx_handle = efx_dl_handle(efx_dev);

	EFX_INFO(efx_handle->efx, "%s driverlink client unregistering\n",
		 efx_dev->driver->name);

	if (efx_dev->driver->remove)
		efx_dev->driver->remove(efx_dev);

	list_del(&efx_handle->driver_node);
	list_del(&efx_handle->port_node);

	kfree(efx_handle);
}

/* Attempt to probe the given device with the driver, creating a
 * new &struct efx_dl_device. If the probe routine returns an error,
 * then the &struct efx_dl_device is destroyed */
static void efx_dl_try_add_device(struct efx_nic *efx,
				  struct efx_dl_driver *driver)
{
	struct efx_dl_handle *efx_handle;
	struct efx_dl_device *efx_dev;
	int rc;

	efx_handle = kzalloc(sizeof(*efx_handle), GFP_KERNEL);
	if (!efx_handle)
		goto fail;
	efx_dev = &efx_handle->efx_dev;
	efx_handle->efx = efx;
	efx_dev->driver = driver;
	efx_dev->pci_dev = efx->pci_dev;
	INIT_LIST_HEAD(&efx_handle->port_node);
	INIT_LIST_HEAD(&efx_handle->driver_node);

	rc = driver->probe(efx_dev, efx->net_dev,
			   efx->dl_info, efx->silicon_rev);
	if (rc)
		goto fail;

	list_add_tail(&efx_handle->driver_node, &driver->device_list);
	list_add_tail(&efx_handle->port_node, &efx->dl_device_list);

	EFX_INFO(efx, "%s driverlink client registered\n", driver->name);
	return;

 fail:
	EFX_INFO(efx, "%s driverlink client skipped\n", driver->name);

	kfree(efx_handle);
}

/* Unregister a driver from the driverlink layer, calling the
 * driver's remove() callback for every attached device */
void efx_dl_unregister_driver(struct efx_dl_driver *driver)
{
	struct efx_dl_handle *efx_handle, *efx_handle_n;

	printk(KERN_INFO "Efx driverlink unregistering %s driver\n",
		 driver->name);

	mutex_lock(&efx_driverlink_lock);

	list_for_each_entry_safe(efx_handle, efx_handle_n,
				 &driver->device_list, driver_node)
		efx_dl_del_device(&efx_handle->efx_dev);

	list_del(&driver->node);

	mutex_unlock(&efx_driverlink_lock);
}
EXPORT_SYMBOL(efx_dl_unregister_driver);

/* Register a new driver with the driverlink layer. The driver's
 * probe routine will be called for every attached nic. */
int efx_dl_register_driver(struct efx_dl_driver *driver)
{
	struct efx_nic *efx;
	int rc;

	printk(KERN_INFO "Efx driverlink registering %s driver\n",
		 driver->name);

	INIT_LIST_HEAD(&driver->node);
	INIT_LIST_HEAD(&driver->device_list);

	rc = mutex_lock_interruptible(&efx_driverlink_lock);
	if (rc)
		return rc;

	list_add_tail(&driver->node, &efx_driver_list);
	list_for_each_entry(efx, &efx_port_list, dl_node)
		efx_dl_try_add_device(efx, driver);

	mutex_unlock(&efx_driverlink_lock);

	return 0;
}
EXPORT_SYMBOL(efx_dl_register_driver);

void efx_dl_unregister_nic(struct efx_nic *efx)
{
	struct efx_dl_handle *efx_handle, *efx_handle_n;

	mutex_lock(&efx_driverlink_lock);

	list_for_each_entry_safe_reverse(efx_handle, efx_handle_n,
					 &efx->dl_device_list,
					 port_node)
		efx_dl_del_device(&efx_handle->efx_dev);

	list_del(&efx->dl_node);

	mutex_unlock(&efx_driverlink_lock);
}

int efx_dl_register_nic(struct efx_nic *efx)
{
	struct efx_dl_driver *driver;
	int rc;

	rc = mutex_lock_interruptible(&efx_driverlink_lock);
	if (rc)
		return rc;

	list_add_tail(&efx->dl_node, &efx_port_list);
	list_for_each_entry(driver, &efx_driver_list, node)
		efx_dl_try_add_device(efx, driver);

	mutex_unlock(&efx_driverlink_lock);

	return 0;
}

/* Dummy callback implementations.
 * To avoid a branch point on the fast-path, the callbacks are always
 * implemented - they are never NULL.
 */
static enum efx_veto efx_dummy_tx_packet_callback(struct efx_dl_device *efx_dev,
						  struct sk_buff *skb)
{
	return EFX_ALLOW_PACKET;
}

static enum efx_veto efx_dummy_rx_packet_callback(struct efx_dl_device *efx_dev,
						  const char *pkt_buf, int len)
{
	return EFX_ALLOW_PACKET;
}

static int efx_dummy_request_mtu_callback(struct efx_dl_device *efx_dev,
					  int new_mtu)
{
	return 0;
}

static void efx_dummy_mtu_changed_callback(struct efx_dl_device *efx_dev,
					   int mtu)
{
	return;
}

static void efx_dummy_event_callback(struct efx_dl_device *efx_dev, void *event)
{
	return;
}

struct efx_dl_callbacks efx_default_callbacks = {
	.tx_packet	= efx_dummy_tx_packet_callback,
	.rx_packet	= efx_dummy_rx_packet_callback,
	.request_mtu	= efx_dummy_request_mtu_callback,
	.mtu_changed	= efx_dummy_mtu_changed_callback,
	.event		= efx_dummy_event_callback,
};

void efx_dl_unregister_callbacks(struct efx_dl_device *efx_dev,
				 struct efx_dl_callbacks *callbacks)
{
	struct efx_dl_handle *efx_handle = efx_dl_handle(efx_dev);
	struct efx_nic *efx = efx_handle->efx;

	efx_suspend(efx);

	EFX_INFO(efx, "removing callback hooks into %s driver\n",
		 efx_dev->driver->name);

	if (callbacks->tx_packet) {
		BUG_ON(efx->dl_cb_dev.tx_packet != efx_dev);
		efx->dl_cb.tx_packet = efx_default_callbacks.tx_packet;
		efx->dl_cb_dev.tx_packet = NULL;
	}
	if (callbacks->rx_packet) {
		BUG_ON(efx->dl_cb_dev.rx_packet != efx_dev);
		efx->dl_cb.rx_packet = efx_default_callbacks.rx_packet;
		efx->dl_cb_dev.rx_packet = NULL;
	}
	if (callbacks->request_mtu) {
		BUG_ON(efx->dl_cb_dev.request_mtu != efx_dev);
		efx->dl_cb.request_mtu = efx_default_callbacks.request_mtu;
		efx->dl_cb_dev.request_mtu = NULL;
	}
	if (callbacks->mtu_changed) {
		BUG_ON(efx->dl_cb_dev.mtu_changed != efx_dev);
		efx->dl_cb.mtu_changed = efx_default_callbacks.mtu_changed;
		efx->dl_cb_dev.mtu_changed = NULL;
	}
	if (callbacks->event) {
		BUG_ON(efx->dl_cb_dev.event != efx_dev);
		efx->dl_cb.event = efx_default_callbacks.event;
		efx->dl_cb_dev.event = NULL;
	}

	efx_resume(efx);
}
EXPORT_SYMBOL(efx_dl_unregister_callbacks);

int efx_dl_register_callbacks(struct efx_dl_device *efx_dev,
			      struct efx_dl_callbacks *callbacks)
{
	struct efx_dl_handle *efx_handle = efx_dl_handle(efx_dev);
	struct efx_nic *efx = efx_handle->efx;
	int rc = 0;

	efx_suspend(efx);

	/* Check that the requested callbacks are not already hooked. */
	if ((callbacks->tx_packet && efx->dl_cb_dev.tx_packet) ||
	    (callbacks->rx_packet && efx->dl_cb_dev.rx_packet) ||
	    (callbacks->request_mtu && efx->dl_cb_dev.request_mtu) ||
	    (callbacks->mtu_changed && efx->dl_cb_dev.mtu_changed) ||
	    (callbacks->event && efx->dl_cb_dev.event)) {
		rc = -EBUSY;
		goto out;
	}

	EFX_INFO(efx, "adding callback hooks to %s driver\n",
		 efx_dev->driver->name);

	/* Hook in the requested callbacks, leaving any NULL members
	 * referencing the members of @efx_default_callbacks */
	if (callbacks->tx_packet) {
		efx->dl_cb.tx_packet = callbacks->tx_packet;
		efx->dl_cb_dev.tx_packet = efx_dev;
	}
	if (callbacks->rx_packet) {
		efx->dl_cb.rx_packet = callbacks->rx_packet;
		efx->dl_cb_dev.rx_packet = efx_dev;
	}
	if (callbacks->request_mtu) {
		efx->dl_cb.request_mtu = callbacks->request_mtu;
		efx->dl_cb_dev.request_mtu = efx_dev;
	}
	if (callbacks->mtu_changed) {
		efx->dl_cb.mtu_changed = callbacks->mtu_changed;
		efx->dl_cb_dev.mtu_changed = efx_dev;
	}
	if (callbacks->event) {
		efx->dl_cb.event = callbacks->event;
		efx->dl_cb_dev.event = efx_dev;
	}

 out:
	efx_resume(efx);

	return rc;
}
EXPORT_SYMBOL(efx_dl_register_callbacks);

void efx_dl_schedule_reset(struct efx_dl_device *efx_dev)
{
	struct efx_dl_handle *efx_handle = efx_dl_handle(efx_dev);
	struct efx_nic *efx = efx_handle->efx;

	efx_schedule_reset(efx, RESET_TYPE_ALL);
}
EXPORT_SYMBOL(efx_dl_schedule_reset);

void efx_dl_reset_unlock(void)
{
	mutex_unlock(&efx_driverlink_lock);
}

/* Suspend ready for reset, serialising against all the driverlink interfacse
 * and calling the suspend() callback of every registered driver */
void efx_dl_reset_suspend(struct efx_nic *efx)
{
	struct efx_dl_handle *efx_handle;
	struct efx_dl_device *efx_dev;

	mutex_lock(&efx_driverlink_lock);

	list_for_each_entry_reverse(efx_handle,
				    &efx->dl_device_list,
				    port_node) {
		efx_dev = &efx_handle->efx_dev;
		if (efx_dev->driver->reset_suspend)
			efx_dev->driver->reset_suspend(efx_dev);
	}
}

/* Resume after a reset, calling the resume() callback of every registered
 * driver, and releasing @Efx_driverlink_lock acquired in
 * efx_dl_reset_resume() */
void efx_dl_reset_resume(struct efx_nic *efx, int ok)
{
	struct efx_dl_handle *efx_handle;
	struct efx_dl_device *efx_dev;

	list_for_each_entry(efx_handle, &efx->dl_device_list,
			    port_node) {
		efx_dev = &efx_handle->efx_dev;
		if (efx_dev->driver->reset_resume)
			efx_dev->driver->reset_resume(efx_dev, ok);
	}

	mutex_unlock(&efx_driverlink_lock);
}
