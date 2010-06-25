/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#include <xen/evtchn.h>
#include <linux/mutex.h>
#include <linux/delay.h>

/* drivers/xen/netback/common.h */
#include "common.h"

#include "accel.h"
#include "accel_solarflare.h"
#include "accel_util.h"

#define NODENAME_PATH_FMT "backend/vif/%d/%d"

#define NETBACK_ACCEL_FROM_XENBUS_DEVICE(_dev) (struct netback_accel *) \
	((struct backend_info *)dev_get_drvdata(&(_dev)->dev))->netback_accel_priv

/* List of all the bends currently in existence. */
struct netback_accel *bend_list = NULL;
DEFINE_MUTEX(bend_list_mutex);

/* Put in bend_list.  Must hold bend_list_mutex */
static void link_bend(struct netback_accel *bend)
{
	bend->next_bend = bend_list;
	bend_list = bend;
}

/* Remove from bend_list,  Must hold bend_list_mutex */
static void unlink_bend(struct netback_accel *bend)
{
	struct netback_accel *tmp = bend_list;
	struct netback_accel *prev = NULL;
	while (tmp != NULL) {
		if (tmp == bend) {
			if (prev != NULL)
				prev->next_bend = bend->next_bend;
			else
				bend_list = bend->next_bend;
			return;
		}
		prev = tmp;
		tmp = tmp->next_bend;
	}
}


/* Demultiplex a message IRQ from the frontend driver.  */
static irqreturn_t msgirq_from_frontend(int irq, void *context)
{
	struct xenbus_device *dev = context;
	struct netback_accel *bend = NETBACK_ACCEL_FROM_XENBUS_DEVICE(dev);
	VPRINTK("irq %d from device %s\n", irq, dev->nodename);
	schedule_work(&bend->handle_msg);
	return IRQ_HANDLED;
}


/*
 * Demultiplex an IRQ from the frontend driver.  This is never used
 * functionally, but we need it to pass to the bind function, and may
 * get called spuriously
 */
static irqreturn_t netirq_from_frontend(int irq, void *context)
{
	VPRINTK("netirq %d from device %s\n", irq,
		((struct xenbus_device *)context)->nodename);
	
	return IRQ_HANDLED;
}


/* Read the limits values of the xenbus structure. */
static 
void cfg_hw_quotas(struct xenbus_device *dev, struct netback_accel *bend)
{
	int err = xenbus_gather
		(XBT_NIL, dev->nodename,
		 "limits/max-filters", "%d", &bend->quotas.max_filters,
		 "limits/max-buf-pages", "%d", &bend->quotas.max_buf_pages,
		 "limits/max-mcasts", "%d", &bend->quotas.max_mcasts,
		 NULL);
	if (err) {
		/*
		 * TODO what if they have previously been set by the
		 * user?  This will overwrite with defaults.  Maybe
		 * not what we want to do, but useful in startup
		 * case 
		 */
		DPRINTK("Failed to read quotas from xenbus, using defaults\n");
		bend->quotas.max_filters = NETBACK_ACCEL_DEFAULT_MAX_FILTERS;
		bend->quotas.max_buf_pages = sfc_netback_max_pages;
		bend->quotas.max_mcasts = NETBACK_ACCEL_DEFAULT_MAX_MCASTS;
	}

	return;
}


static void bend_config_accel_change(struct xenbus_watch *watch,
				     const char **vec, unsigned int len)
{
	struct netback_accel *bend;

	bend = container_of(watch, struct netback_accel, config_accel_watch);

	mutex_lock(&bend->bend_mutex);
	if (bend->config_accel_watch.node != NULL) {
		struct xenbus_device *dev = 
			(struct xenbus_device *)bend->hdev_data;
		DPRINTK("Watch matched, got dev %p otherend %p\n",
			dev, dev->otherend);
		if(!xenbus_exists(XBT_NIL, watch->node, "")) {
			DPRINTK("Ignoring watch as otherend seems invalid\n");
			goto out;
		}
		
		cfg_hw_quotas(dev, bend);
	}
 out:
	mutex_unlock(&bend->bend_mutex);
	return;
}


/*
 * Setup watch on "limits" in the backend vif info to know when
 * configuration has been set
 */
static int setup_config_accel_watch(struct xenbus_device *dev,
				    struct netback_accel *bend)
{
	int err;

	VPRINTK("Setting watch on %s/%s\n", dev->nodename, "limits");

	err = xenbus_watch_path2(dev, dev->nodename, "limits", 
				 &bend->config_accel_watch, 
				 bend_config_accel_change);

	if (err) {
		EPRINTK("%s: Failed to register xenbus watch: %d\n",
			__FUNCTION__, err);
		bend->config_accel_watch.node = NULL;
		return err;
	}
	return 0;
}


static int 
cfg_frontend_info(struct xenbus_device *dev, struct netback_accel *bend,
		  int *grants)
{
	/* Get some info from xenbus on the event channel and shmem grant */
	int err = xenbus_gather(XBT_NIL, dev->otherend, 
				"accel-msg-channel", "%u", &bend->msg_channel, 
				"accel-ctrl-page", "%d", &(grants[0]),
				"accel-msg-page", "%d", &(grants[1]),
				"accel-net-channel", "%u", &bend->net_channel,
				NULL);
	if (err)
		EPRINTK("failed to read event channels or shmem grant: %d\n",
			err);
	else
		DPRINTK("got event chan %d and net chan %d from frontend\n",
			bend->msg_channel, bend->net_channel);
	return err;
}


/* Setup all the comms needed to chat with the front end driver */
static int setup_vnic(struct xenbus_device *dev)
{
	struct netback_accel *bend;
	int grants[2], err, msgs_per_queue;

	bend = NETBACK_ACCEL_FROM_XENBUS_DEVICE(dev);

	err = cfg_frontend_info(dev, bend, grants);
	if (err)
		goto fail1;

	/*
	 * If we get here, both frontend Connected and configuration
	 * options available.  All is well.
	 */

	/* Get the hardware quotas for the VNIC in question.  */
	cfg_hw_quotas(dev, bend);

	/* Set up the deferred work handlers */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
	INIT_WORK(&bend->handle_msg, 
		  netback_accel_msg_rx_handler);
#else
	INIT_WORK(&bend->handle_msg, 
		  netback_accel_msg_rx_handler,
		  (void*)bend);
#endif

	/* Request the frontend mac */
	err = net_accel_xen_net_read_mac(dev, bend->mac);
	if (err)
		goto fail2;

	/* Set up the shared page. */
	bend->shared_page = net_accel_map_grants_contig(dev, grants, 2, 
							&bend->sh_pages_unmap);

	if (bend->shared_page == NULL) {
		EPRINTK("failed to map shared page for %s\n", dev->otherend);
		err = -ENOMEM;
		goto fail2;
	}

	/* Initialise the shared page(s) used for comms */
	net_accel_msg_init_page(bend->shared_page, PAGE_SIZE, 
				(bend->net_dev->flags & IFF_UP) && 
				(netif_carrier_ok(bend->net_dev)));

	msgs_per_queue = (PAGE_SIZE/2) / sizeof(struct net_accel_msg);

	net_accel_msg_init_queue
		(&bend->to_domU, &bend->shared_page->queue0,
		 (struct net_accel_msg *)((__u8*)bend->shared_page + PAGE_SIZE),
		 msgs_per_queue);

	net_accel_msg_init_queue
		(&bend->from_domU, &bend->shared_page->queue1, 
		 (struct net_accel_msg *)((__u8*)bend->shared_page + 
					  (3 * PAGE_SIZE / 2)),
		 msgs_per_queue);

	/* Bind the message event channel to a handler
	 *
	 * Note that we will probably get a spurious interrupt when we
	 * do this, so it must not be done until we have set up
	 * everything we need to handle it.
	 */
	err = bind_interdomain_evtchn_to_irqhandler(dev->otherend_id,
						    bend->msg_channel,
						    msgirq_from_frontend,
						    0,
						    "netback_accel",
						    dev);
	if (err < 0) {
		EPRINTK("failed to bind event channel: %d\n", err);
		goto fail3;
	}
	else
		bend->msg_channel_irq = err;

	/* TODO: No need to bind this evtchn to an irq. */
	err = bind_interdomain_evtchn_to_irqhandler(dev->otherend_id,
						    bend->net_channel,
						    netirq_from_frontend,
						    0,
						    "netback_accel",
						    dev);
	if (err < 0) {
		EPRINTK("failed to bind net channel: %d\n", err);
		goto fail4;
	}  
	else
		bend->net_channel_irq = err;

	/*
	 * Grab ourselves an entry in the forwarding hash table. We do
	 * this now so we don't have the embarassmesnt of sorting out
	 * an allocation failure while at IRQ. Because we pass NULL as
	 * the context, the actual hash lookup will succeed for this
	 * NIC, but the check for somewhere to forward to will
	 * fail. This is necessary to prevent forwarding before
	 * hardware resources are set up
	 */
	err = netback_accel_fwd_add(bend->mac, NULL, bend->fwd_priv);
	if (err) {
		EPRINTK("failed to add to fwd hash table\n");
		goto fail5;
	}

	/*
	 * Say hello to frontend.  Important to do this straight after
	 * obtaining the message queue as otherwise we are vulnerable
	 * to an evil frontend sending a HELLO-REPLY before we've sent
	 * the HELLO and confusing us
	 */
	netback_accel_msg_tx_hello(bend, NET_ACCEL_MSG_VERSION);
	return 0;

 fail5:
	unbind_from_irqhandler(bend->net_channel_irq, dev);
 fail4:
	unbind_from_irqhandler(bend->msg_channel_irq, dev);
 fail3:
	net_accel_unmap_grants_contig(dev, bend->sh_pages_unmap);
	bend->shared_page = NULL;
	bend->sh_pages_unmap = NULL;
 fail2:
 fail1:
	return err;
}


static int read_nicname(struct xenbus_device *dev, struct netback_accel *bend)
{
	int len;

	/* nic name used to select interface used for acceleration */
	bend->nicname = xenbus_read(XBT_NIL, dev->nodename, "accel", &len);
	if (IS_ERR(bend->nicname))
		return PTR_ERR(bend->nicname);

	return 0;
}

static const char *frontend_name = "sfc_netfront";

static int publish_frontend_name(struct xenbus_device *dev)
{
	struct xenbus_transaction tr;
	int err;
	
	/* Publish the name of the frontend driver */
	do {
		err = xenbus_transaction_start(&tr);
		if (err != 0) { 
			EPRINTK("%s: transaction start failed\n", __FUNCTION__);
			return err;
		}
		err = xenbus_printf(tr, dev->nodename, "accel-frontend", 
				    "%s", frontend_name);
		if (err != 0) {
			EPRINTK("%s: xenbus_printf failed\n", __FUNCTION__);
			xenbus_transaction_end(tr, 1);
			return err;
		}
		err = xenbus_transaction_end(tr, 0);
	} while (err == -EAGAIN);
	
	if (err != 0) {
		EPRINTK("failed to end frontend name transaction\n");
		return err;
	}
	return 0;
}


static int unpublish_frontend_name(struct xenbus_device *dev)
{
	struct xenbus_transaction tr;
	int err;

	do {
		err = xenbus_transaction_start(&tr);
		if (err != 0)
			break;
		err = xenbus_rm(tr, dev->nodename, "accel-frontend");
		if (err != 0) {
			xenbus_transaction_end(tr, 1);
			break;
		}
		err = xenbus_transaction_end(tr, 0);
	} while (err == -EAGAIN);

	return err;
}


static void cleanup_vnic(struct netback_accel *bend)
{
	struct xenbus_device *dev;

	dev = (struct xenbus_device *)bend->hdev_data;

	DPRINTK("%s: bend %p dev %p\n", __FUNCTION__, bend, dev);

	DPRINTK("%s: Remove %p's mac from fwd table...\n", 
		__FUNCTION__, bend);
	netback_accel_fwd_remove(bend->mac, bend->fwd_priv);

	/* Free buffer table allocations */
	netback_accel_remove_buffers(bend);

	DPRINTK("%s: Release hardware resources...\n", __FUNCTION__);
	if (bend->accel_shutdown)
		bend->accel_shutdown(bend);

	if (bend->net_channel_irq) {
		unbind_from_irqhandler(bend->net_channel_irq, dev);
		bend->net_channel_irq = 0;
	}

	if (bend->msg_channel_irq) {
		unbind_from_irqhandler(bend->msg_channel_irq, dev);
		bend->msg_channel_irq = 0;
	}

	if (bend->sh_pages_unmap) {
		DPRINTK("%s: Unmap grants %p\n", __FUNCTION__, 
			bend->sh_pages_unmap);
		net_accel_unmap_grants_contig(dev, bend->sh_pages_unmap);
		bend->sh_pages_unmap = NULL;
		bend->shared_page = NULL;
	}
}


/*************************************************************************/

/*
 * The following code handles accelstate changes between the frontend
 * and the backend.  It calls setup_vnic and cleanup_vnic in matching
 * pairs in response to transitions.
 *
 * Valid state transitions for Dom0 are as follows:
 *
 * Closed->Init       on probe or in response to Init from domU
 * Closed->Closing    on error/remove
 *
 * Init->Connected    in response to Connected from domU
 * Init->Closing      on error/remove or in response to Closing from domU
 *
 * Connected->Closing on error/remove or in response to Closing from domU
 *
 * Closing->Closed    in response to Closed from domU
 *
 */


static void netback_accel_frontend_changed(struct xenbus_device *dev,
					   XenbusState frontend_state)
{
	struct netback_accel *bend = NETBACK_ACCEL_FROM_XENBUS_DEVICE(dev);
	XenbusState backend_state;

	DPRINTK("%s: changing from %s to %s. nodename %s, otherend %s\n",
		__FUNCTION__, xenbus_strstate(bend->frontend_state),
		xenbus_strstate(frontend_state),dev->nodename, dev->otherend);

	/*
	 * Ignore duplicate state changes.  This can happen if the
	 * frontend changes state twice in quick succession and the
	 * first watch fires in the backend after the second
	 * transition has completed.
	 */
	if (bend->frontend_state == frontend_state)
		return;

	bend->frontend_state = frontend_state;
	backend_state = bend->backend_state;

	switch (frontend_state) {
	case XenbusStateInitialising:
		if (backend_state == XenbusStateClosed &&
		    !bend->removing)
			backend_state = XenbusStateInitialising;
		break;

	case XenbusStateConnected:
		if (backend_state == XenbusStateInitialising) {
			if (!bend->vnic_is_setup &&
			    setup_vnic(dev) == 0) {
				bend->vnic_is_setup = 1;
				backend_state = XenbusStateConnected;
			} else {
				backend_state = XenbusStateClosing;
			}
		}
		break;

	case XenbusStateInitWait:
	case XenbusStateInitialised:
	default:
		DPRINTK("Unknown state %s (%d) from frontend.\n",
			xenbus_strstate(frontend_state), frontend_state);
		/* Unknown state.  Fall through. */
	case XenbusStateClosing:
		if (backend_state != XenbusStateClosed)
			backend_state = XenbusStateClosing;

		/*
		 * The bend will now persist (with watches active) in
		 * case the frontend comes back again, eg. after
		 * frontend module reload or suspend/resume
		 */

		break;

	case XenbusStateUnknown:
	case XenbusStateClosed:
		if (bend->vnic_is_setup) {
			bend->vnic_is_setup = 0;
			cleanup_vnic(bend);
		}

		if (backend_state == XenbusStateClosing)
			backend_state = XenbusStateClosed;
		break;
	}

	if (backend_state != bend->backend_state) {
		DPRINTK("Switching from state %s (%d) to %s (%d)\n",
			xenbus_strstate(bend->backend_state),
			bend->backend_state,
			xenbus_strstate(backend_state), backend_state);
		bend->backend_state = backend_state;
		net_accel_update_state(dev, backend_state);
	}

	wake_up(&bend->state_wait_queue);
}


/* accelstate on the frontend's xenbus node has changed */
static void bend_domu_accel_change(struct xenbus_watch *watch,
				   const char **vec, unsigned int len)
{
	int state;
	struct netback_accel *bend;

	bend = container_of(watch, struct netback_accel, domu_accel_watch);
	if (bend->domu_accel_watch.node != NULL) {
		struct xenbus_device *dev = 
			(struct xenbus_device *)bend->hdev_data;
		VPRINTK("Watch matched, got dev %p otherend %p\n",
			dev, dev->otherend);
		/*
		 * dev->otherend != NULL check to protect against
		 * watch firing when domain goes away and we haven't
		 * yet cleaned up
		 */
		if (!dev->otherend ||
		    !xenbus_exists(XBT_NIL, watch->node, "") ||
		    strncmp(dev->otherend, vec[XS_WATCH_PATH],
			    strlen(dev->otherend))) {
			DPRINTK("Ignoring watch as otherend seems invalid\n");
			return;
		}

		mutex_lock(&bend->bend_mutex);

		xenbus_scanf(XBT_NIL, dev->otherend, "accelstate", "%d", 
			     &state);
		netback_accel_frontend_changed(dev, state);

		mutex_unlock(&bend->bend_mutex);
	}
}

/* Setup watch on frontend's accelstate */
static int setup_domu_accel_watch(struct xenbus_device *dev,
				  struct netback_accel *bend)
{
	int err;

	VPRINTK("Setting watch on %s/%s\n", dev->otherend, "accelstate");

	err = xenbus_watch_path2(dev, dev->otherend, "accelstate", 
				 &bend->domu_accel_watch, 
				 bend_domu_accel_change);
	if (err) {
		EPRINTK("%s: Failed to register xenbus watch: %d\n",
			__FUNCTION__, err);
		goto fail;
	}
	return 0;
 fail:
	bend->domu_accel_watch.node = NULL;
	return err;
}


int netback_accel_probe(struct xenbus_device *dev)
{
	struct netback_accel *bend;
	struct backend_info *binfo;
	int err;

	DPRINTK("%s: passed device %s\n", __FUNCTION__, dev->nodename);

	/* Allocate structure to store all our state... */
	bend = kzalloc(sizeof(struct netback_accel), GFP_KERNEL);
	if (bend == NULL) {
		DPRINTK("%s: no memory for bend\n", __FUNCTION__);
		return -ENOMEM;
	}
	
	mutex_init(&bend->bend_mutex);

	mutex_lock(&bend->bend_mutex);

	/* ...and store it where we can get at it */
	binfo = dev_get_drvdata(&dev->dev);
	binfo->netback_accel_priv = bend;
	/* And vice-versa */
	bend->hdev_data = dev;

	DPRINTK("%s: Adding bend %p to list\n", __FUNCTION__, bend);
	
	init_waitqueue_head(&bend->state_wait_queue);
	bend->vnic_is_setup = 0;
	bend->frontend_state = XenbusStateUnknown;
	bend->backend_state = XenbusStateClosed;
	bend->removing = 0;

	sscanf(dev->nodename, NODENAME_PATH_FMT, &bend->far_end, 
	       &bend->vif_num);

	err = read_nicname(dev, bend);
	if (err) {
		/*
		 * Technically not an error, just means we're not 
		 * supposed to accelerate this
		 */
		DPRINTK("failed to get device name\n");
		goto fail_nicname;
	}

	/*
	 * Look up the device name in the list of NICs provided by
	 * driverlink to get the hardware type.
	 */
	err = netback_accel_sf_hwtype(bend);
	if (err) {
		/*
		 * Technically not an error, just means we're not
		 * supposed to accelerate this, probably belongs to
		 * some other backend
		 */
		DPRINTK("failed to match device name\n");
		goto fail_init_type;
	}

	err = publish_frontend_name(dev);
	if (err)
		goto fail_publish;

	err = netback_accel_debugfs_create(bend);
	if (err)
		goto fail_debugfs;
	
	mutex_unlock(&bend->bend_mutex);

	err = setup_config_accel_watch(dev, bend);
	if (err)
		goto fail_config_watch;

	err = setup_domu_accel_watch(dev, bend);
	if (err)
		goto fail_domu_watch;

	/*
	 * Indicate to the other end that we're ready to start unless
	 * the watch has already fired.
	 */
	mutex_lock(&bend->bend_mutex);
	if (bend->backend_state == XenbusStateClosed) {
		bend->backend_state = XenbusStateInitialising;
		net_accel_update_state(dev, XenbusStateInitialising);
	}
	mutex_unlock(&bend->bend_mutex);

	mutex_lock(&bend_list_mutex);
	link_bend(bend);
	mutex_unlock(&bend_list_mutex);

	return 0;

fail_domu_watch:

	unregister_xenbus_watch(&bend->config_accel_watch);
	kfree(bend->config_accel_watch.node);
fail_config_watch:

	/*
	 * Flush the scheduled work queue before freeing bend to get
	 * rid of any pending netback_accel_msg_rx_handler()
	 */
	flush_scheduled_work();

	mutex_lock(&bend->bend_mutex);
	net_accel_update_state(dev, XenbusStateUnknown);
	netback_accel_debugfs_remove(bend);
fail_debugfs:

	unpublish_frontend_name(dev);
fail_publish:

	/* No need to reverse netback_accel_sf_hwtype. */
fail_init_type:

	kfree(bend->nicname);
fail_nicname:
	binfo->netback_accel_priv = NULL;
	mutex_unlock(&bend->bend_mutex);
	kfree(bend);
	return err;
}


int netback_accel_remove(struct xenbus_device *dev)
{
	struct backend_info *binfo;
	struct netback_accel *bend; 
	int frontend_state;

	binfo = dev_get_drvdata(&dev->dev);
	bend = (struct netback_accel *) binfo->netback_accel_priv;

	DPRINTK("%s: dev %p bend %p\n", __FUNCTION__, dev, bend);
	
	BUG_ON(bend == NULL);
	
	mutex_lock(&bend_list_mutex);
	unlink_bend(bend);
	mutex_unlock(&bend_list_mutex);

	mutex_lock(&bend->bend_mutex);

	/* Reject any requests to connect. */
	bend->removing = 1;

	/*
	 * Switch to closing to tell the other end that we're going
	 * away.
	 */
	if (bend->backend_state != XenbusStateClosing) {
		bend->backend_state = XenbusStateClosing;
		net_accel_update_state(dev, XenbusStateClosing);
	}

	frontend_state = (int)XenbusStateUnknown;
	xenbus_scanf(XBT_NIL, dev->otherend, "accelstate", "%d",
		     &frontend_state);

	mutex_unlock(&bend->bend_mutex);

	/*
	 * Wait until this end goes to the closed state.  This happens
	 * in response to the other end going to the closed state.
	 * Don't bother doing this if the other end is already closed
	 * because if it is then there is nothing to do.
	 */
	if (frontend_state != (int)XenbusStateClosed &&
	    frontend_state != (int)XenbusStateUnknown)
		wait_event(bend->state_wait_queue,
			   bend->backend_state == XenbusStateClosed);

	unregister_xenbus_watch(&bend->domu_accel_watch);
	kfree(bend->domu_accel_watch.node);

	unregister_xenbus_watch(&bend->config_accel_watch);
	kfree(bend->config_accel_watch.node);

	/*
	 * Flush the scheduled work queue before freeing bend to get
	 * rid of any pending netback_accel_msg_rx_handler()
	 */
	flush_scheduled_work();

	mutex_lock(&bend->bend_mutex);

	/* Tear down the vnic if it was set up. */
	if (bend->vnic_is_setup) {
		bend->vnic_is_setup = 0;
		cleanup_vnic(bend);
	}

	bend->backend_state = XenbusStateUnknown;
	net_accel_update_state(dev, XenbusStateUnknown);

	netback_accel_debugfs_remove(bend);

	unpublish_frontend_name(dev);

	kfree(bend->nicname);

	binfo->netback_accel_priv = NULL;

	mutex_unlock(&bend->bend_mutex);

	kfree(bend);

	return 0;
}


void netback_accel_shutdown_bends(void)
{
	mutex_lock(&bend_list_mutex);
	/*
	 * I think we should have had a remove callback for all
	 * interfaces before being allowed to unload the module
	 */
	BUG_ON(bend_list != NULL);
	mutex_unlock(&bend_list_mutex);
}


void netback_accel_set_closing(struct netback_accel *bend) 
{

	bend->backend_state = XenbusStateClosing;
	net_accel_update_state((struct xenbus_device *)bend->hdev_data,
			       XenbusStateClosing);
}
