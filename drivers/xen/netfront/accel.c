/******************************************************************************
 * Virtual network driver for conversing with remote driver backends.
 *
 * Copyright (C) 2007 Solarflare Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <asm/hypervisor.h>
#include <xen/xenbus.h>

#include "netfront.h"

#define DPRINTK(fmt, args...)				\
	pr_debug("netfront/accel (%s:%d) " fmt,		\
	       __FUNCTION__, __LINE__, ##args)
#define IPRINTK(fmt, args...)				\
	printk(KERN_INFO "netfront/accel: " fmt, ##args)
#define WPRINTK(fmt, args...)				\
	printk(KERN_WARNING "netfront/accel: " fmt, ##args)

static int netfront_remove_accelerator(struct netfront_info *np,
				       struct xenbus_device *dev);
static int netfront_load_accelerator(struct netfront_info *np, 
				     struct xenbus_device *dev, 
				     const char *frontend);

/*
 * List of all netfront accelerator plugin modules available.  Each
 * list entry is of type struct netfront_accelerator.
 */ 
static struct list_head accelerators_list;

/* Lock to protect access to accelerators_list */
static spinlock_t accelerators_lock;

/* Workqueue to process acceleration configuration changes */
struct workqueue_struct *accel_watch_workqueue;

/* Mutex to prevent concurrent loads and suspends, etc. */
DEFINE_MUTEX(accelerator_mutex);

void netif_init_accel(void)
{
	INIT_LIST_HEAD(&accelerators_list);
	spin_lock_init(&accelerators_lock);

	accel_watch_workqueue = create_workqueue("net_accel");
}

void netif_exit_accel(void)
{
	struct netfront_accelerator *accelerator, *tmp;
	unsigned long flags;

	flush_workqueue(accel_watch_workqueue);
	destroy_workqueue(accel_watch_workqueue);

	spin_lock_irqsave(&accelerators_lock, flags);

	list_for_each_entry_safe(accelerator, tmp, &accelerators_list, link) {
		BUG_ON(!list_empty(&accelerator->vif_states));

		list_del(&accelerator->link);
		kfree(accelerator->frontend);
		kfree(accelerator);
	}

	spin_unlock_irqrestore(&accelerators_lock, flags);
}


/* 
 * Watch the configured accelerator and change plugin if it's modified 
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
static void accel_watch_work(struct work_struct *context)
#else
static void accel_watch_work(void *context)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
	struct netfront_accel_vif_state *vif_state = 
		container_of(context, struct netfront_accel_vif_state, 
			     accel_work);
#else
        struct netfront_accel_vif_state *vif_state = 
		(struct netfront_accel_vif_state *)context;
#endif
	struct netfront_info *np = vif_state->np;
	char *accel_frontend;
	int accel_len, rc = -1;

	mutex_lock(&accelerator_mutex);

	accel_frontend = xenbus_read(XBT_NIL, np->xbdev->otherend, 
				     "accel-frontend", &accel_len);
	if (IS_ERR(accel_frontend)) {
		accel_frontend = NULL;
		netfront_remove_accelerator(np, np->xbdev);
	} else {
		/* If this is the first time, request the accelerator,
		   otherwise only request one if it has changed */
		if (vif_state->accel_frontend == NULL) {
			rc = netfront_load_accelerator(np, np->xbdev, 
						       accel_frontend);
		} else {
			if (strncmp(vif_state->accel_frontend, accel_frontend,
				    accel_len)) {
				netfront_remove_accelerator(np, np->xbdev);
				rc = netfront_load_accelerator(np, np->xbdev, 
							       accel_frontend);
			}
		}
	}

	/* Get rid of previous state and replace with the new name */
	if (vif_state->accel_frontend != NULL)
		kfree(vif_state->accel_frontend);
	vif_state->accel_frontend = accel_frontend;

	mutex_unlock(&accelerator_mutex);

	if (rc == 0) {
		DPRINTK("requesting module %s\n", accel_frontend);
		request_module("%s", accel_frontend);
		/*
		 * Module should now call netfront_accelerator_loaded() once
		 * it's up and running, and we can continue from there 
		 */
	}
}


static void accel_watch_changed(struct xenbus_watch *watch,
				const char **vec, unsigned int len)
{
	struct netfront_accel_vif_state *vif_state = 
		container_of(watch, struct netfront_accel_vif_state,
			     accel_watch);
	queue_work(accel_watch_workqueue, &vif_state->accel_work);
}


void netfront_accelerator_add_watch(struct netfront_info *np)
{
	int err;
	
	/* Check we're not trying to overwrite an existing watch */
	BUG_ON(np->accel_vif_state.accel_watch.node != NULL);

	/* Get a watch on the accelerator plugin */
	err = xenbus_watch_path2(np->xbdev, np->xbdev->otherend, 
				 "accel-frontend", 
				 &np->accel_vif_state.accel_watch,
				 accel_watch_changed);
	if (err) {
		DPRINTK("%s: Failed to register accel watch: %d\n",
                        __FUNCTION__, err);
		np->accel_vif_state.accel_watch.node = NULL;
        }
}


static
void netfront_accelerator_remove_watch(struct netfront_info *np)
{
	struct netfront_accel_vif_state *vif_state = &np->accel_vif_state;

	/* Get rid of watch on accelerator plugin */
	if (vif_state->accel_watch.node != NULL) {
		unregister_xenbus_watch(&vif_state->accel_watch);
		kfree(vif_state->accel_watch.node);
		vif_state->accel_watch.node = NULL;

		flush_workqueue(accel_watch_workqueue);

		/* Clean up any state left from watch */
		if (vif_state->accel_frontend != NULL) {
			kfree(vif_state->accel_frontend);
			vif_state->accel_frontend = NULL;
		}
	}	
}


/* 
 * Initialise the accel_vif_state field in the netfront state
 */ 
void init_accelerator_vif(struct netfront_info *np,
			  struct xenbus_device *dev)
{
	np->accelerator = NULL;

	/* It's assumed that these things don't change */
	np->accel_vif_state.np = np;
	np->accel_vif_state.dev = dev;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
	INIT_WORK(&np->accel_vif_state.accel_work, accel_watch_work);
#else
	INIT_WORK(&np->accel_vif_state.accel_work, accel_watch_work, 
		  &np->accel_vif_state);
#endif
}


/*
 * Compare a frontend description string against an accelerator to see
 * if they match.  Would ultimately be nice to replace the string with
 * a unique numeric identifier for each accelerator.
 */
static int match_accelerator(const char *frontend, 
			     struct netfront_accelerator *accelerator)
{
	return strcmp(frontend, accelerator->frontend) == 0;
}


/* 
 * Add a frontend vif to the list of vifs that is using a netfront
 * accelerator plugin module.
 */
static void add_accelerator_vif(struct netfront_accelerator *accelerator,
				struct netfront_info *np)
{
	unsigned long flags;

	/* Need lock to write list */
	spin_lock_irqsave(&accelerator->vif_states_lock, flags);

	if (np->accelerator == NULL) {
		np->accelerator = accelerator;
		
		list_add(&np->accel_vif_state.link, &accelerator->vif_states);
	} else {
		/* 
		 * May get here legitimately if suspend_cancel is
		 * called, but in that case configuration should not
		 * have changed
		 */
		BUG_ON(np->accelerator != accelerator);
	}

	spin_unlock_irqrestore(&accelerator->vif_states_lock, flags);
}


/*
 * Initialise the state to track an accelerator plugin module.
 */ 
static int init_accelerator(const char *frontend, 
			    struct netfront_accelerator **result,
			    struct netfront_accel_hooks *hooks)
{
	struct netfront_accelerator *accelerator = 
		kmalloc(sizeof(struct netfront_accelerator), GFP_KERNEL);
	unsigned long flags;
	int frontend_len;

	if (!accelerator) {
		DPRINTK("no memory for accelerator\n");
		return -ENOMEM;
	}

	frontend_len = strlen(frontend) + 1;
	accelerator->frontend = kmalloc(frontend_len, GFP_KERNEL);
	if (!accelerator->frontend) {
		DPRINTK("no memory for accelerator\n");
		kfree(accelerator);
		return -ENOMEM;
	}
	strlcpy(accelerator->frontend, frontend, frontend_len);
	
	INIT_LIST_HEAD(&accelerator->vif_states);
	spin_lock_init(&accelerator->vif_states_lock);

	accelerator->hooks = hooks;

	spin_lock_irqsave(&accelerators_lock, flags);
	list_add(&accelerator->link, &accelerators_list);
	spin_unlock_irqrestore(&accelerators_lock, flags);

	*result = accelerator;

	return 0;
}					


/* 
 * Modify the hooks stored in the per-vif state to match that in the
 * netfront accelerator's state.
 */
static void 
accelerator_set_vif_state_hooks(struct netfront_accel_vif_state *vif_state)
{
	/* This function must be called with the vif_states_lock held */

	DPRINTK("%p\n",vif_state);

	/* Make sure there are no data path operations going on */
	napi_disable(&vif_state->np->napi);
	netif_tx_lock_bh(vif_state->np->netdev);

	vif_state->hooks = vif_state->np->accelerator->hooks;

	netif_tx_unlock_bh(vif_state->np->netdev);
	napi_enable(&vif_state->np->napi);
}


static void accelerator_probe_new_vif(struct netfront_info *np,
				      struct xenbus_device *dev, 
				      struct netfront_accelerator *accelerator)
{
	struct netfront_accel_hooks *hooks;
	unsigned long flags;

	DPRINTK("\n");

	/* Include this frontend device on the accelerator's list */
	add_accelerator_vif(accelerator, np);
	
	hooks = accelerator->hooks;
	
	if (hooks) {
		if (hooks->new_device(np->netdev, dev) == 0) {
			spin_lock_irqsave
				(&accelerator->vif_states_lock, flags);

			accelerator_set_vif_state_hooks(&np->accel_vif_state);

			spin_unlock_irqrestore
				(&accelerator->vif_states_lock, flags);
		}
	}

	return;
}


/*  
 * Request that a particular netfront accelerator plugin is loaded.
 * Usually called as a result of the vif configuration specifying
 * which one to use. Must be called with accelerator_mutex held 
 */
static int netfront_load_accelerator(struct netfront_info *np, 
				     struct xenbus_device *dev, 
				     const char *frontend)
{
	struct netfront_accelerator *accelerator;
	int rc = 0;

	DPRINTK(" %s\n", frontend);

	/* 
	 * Look at list of loaded accelerators to see if the requested
	 * one is already there 
	 */
	list_for_each_entry(accelerator, &accelerators_list, link) {
		if (match_accelerator(frontend, accelerator)) {
			accelerator_probe_new_vif(np, dev, accelerator);
			return 0;
		}
	}

	/* Couldn't find it, so create a new one and load the module */
	if ((rc = init_accelerator(frontend, &accelerator, NULL)) < 0) {
		return rc;
	}

	/* Include this frontend device on the accelerator's list */
	add_accelerator_vif(accelerator, np);

	return rc;
}


/*
 * Go through all the netfront vifs and see if they have requested
 * this accelerator.  Notify the accelerator plugin of the relevant
 * device if so.  Called when an accelerator plugin module is first
 * loaded and connects to netfront.
 */
static void 
accelerator_probe_vifs(struct netfront_accelerator *accelerator,
		       struct netfront_accel_hooks *hooks)
{
	struct netfront_accel_vif_state *vif_state, *tmp;
	unsigned long flags;

	DPRINTK("%p\n", accelerator);

	/* 
	 * Store the hooks for future calls to probe a new device, and
	 * to wire into the vif_state once the accelerator plugin is
	 * ready to accelerate each vif
	 */
	BUG_ON(hooks == NULL);
	accelerator->hooks = hooks;

	/* 
	 *  currently hold accelerator_mutex, so don't need
	 *  vif_states_lock to read the list
	 */
	list_for_each_entry_safe(vif_state, tmp, &accelerator->vif_states,
				 link) {
		struct netfront_info *np = vif_state->np;
		
		if (hooks->new_device(np->netdev, vif_state->dev) == 0) {
			spin_lock_irqsave
				(&accelerator->vif_states_lock, flags);

			accelerator_set_vif_state_hooks(vif_state);

			spin_unlock_irqrestore
				(&accelerator->vif_states_lock, flags);
		}
	}
}


/* 
 * Called by the netfront accelerator plugin module when it has loaded 
 */
int netfront_accelerator_loaded(int version, const char *frontend, 
				struct netfront_accel_hooks *hooks)
{
	struct netfront_accelerator *accelerator;

	if (is_initial_xendomain())
		return -EINVAL;

	if (version != NETFRONT_ACCEL_VERSION) {
		if (version > NETFRONT_ACCEL_VERSION) {
			/* Caller has higher version number, leave it
			   up to them to decide whether to continue.
			   They can re-call with a lower number if
			   they're happy to be compatible with us */
			return NETFRONT_ACCEL_VERSION;
		} else {
			/* We have a more recent version than caller.
			   Currently reject, but may in future be able
			   to be backwardly compatible */
			return -EPROTO;
		}
	}

	mutex_lock(&accelerator_mutex);

	/* 
	 * Look through list of accelerators to see if it has already
	 * been requested
	 */
	list_for_each_entry(accelerator, &accelerators_list, link) {
		if (match_accelerator(frontend, accelerator)) {
			accelerator_probe_vifs(accelerator, hooks);
			goto out;
		}
	}

	/*
	 * If it wasn't in the list, add it now so that when it is
	 * requested the caller will find it
	 */
	DPRINTK("Couldn't find matching accelerator (%s)\n",
		frontend);

	init_accelerator(frontend, &accelerator, hooks);

 out:
	mutex_unlock(&accelerator_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(netfront_accelerator_loaded);


/* 
 * Remove the hooks from a single vif state.
 */
static void 
accelerator_remove_single_hook(struct netfront_accelerator *accelerator,
			       struct netfront_accel_vif_state *vif_state)
{
	/* Make sure there are no data path operations going on */
	napi_disable(&vif_state->np->napi);
	netif_tx_lock_bh(vif_state->np->netdev);

	/* 
	 * Remove the hooks, but leave the vif_state on the
	 * accelerator's list as that signifies this vif is
	 * interested in using that accelerator if it becomes
	 * available again
	 */
	vif_state->hooks = NULL;
	
	netif_tx_unlock_bh(vif_state->np->netdev);
	napi_enable(&vif_state->np->napi);
}


/* 
 * Safely remove the accelerator function hooks from a netfront state.
 */
static void accelerator_remove_hooks(struct netfront_accelerator *accelerator)
{
	struct netfront_accel_hooks *hooks;
	struct netfront_accel_vif_state *vif_state, *tmp;
	unsigned long flags;

	/* Mutex is held so don't need vif_states_lock to iterate list */
	list_for_each_entry_safe(vif_state, tmp,
				 &accelerator->vif_states,
				 link) {
		spin_lock_irqsave(&accelerator->vif_states_lock, flags);

		if(vif_state->hooks) {
			hooks = vif_state->hooks;
			
			/* Last chance to get statistics from the accelerator */
			hooks->get_stats(vif_state->np->netdev,
					 &vif_state->np->stats);

			spin_unlock_irqrestore(&accelerator->vif_states_lock,
					       flags);

			accelerator_remove_single_hook(accelerator, vif_state);

			accelerator->hooks->remove(vif_state->dev);
		} else {
			spin_unlock_irqrestore(&accelerator->vif_states_lock,
					       flags);
		}
	}
	
	accelerator->hooks = NULL;
}


/* 
 * Called by a netfront accelerator when it is unloaded.  This safely
 * removes the hooks into the plugin and blocks until all devices have
 * finished using it, so on return it is safe to unload.
 */
void netfront_accelerator_stop(const char *frontend)
{
	struct netfront_accelerator *accelerator;
	unsigned long flags;

	mutex_lock(&accelerator_mutex);
	spin_lock_irqsave(&accelerators_lock, flags);

	list_for_each_entry(accelerator, &accelerators_list, link) {
		if (match_accelerator(frontend, accelerator)) {
			spin_unlock_irqrestore(&accelerators_lock, flags);

			accelerator_remove_hooks(accelerator);

			goto out;
		}
	}
	spin_unlock_irqrestore(&accelerators_lock, flags);
 out:
	mutex_unlock(&accelerator_mutex);
}
EXPORT_SYMBOL_GPL(netfront_accelerator_stop);


/* Helper for call_remove and do_suspend */
static int do_remove(struct netfront_info *np, struct xenbus_device *dev,
		     unsigned long *lock_flags)
{
	struct netfront_accelerator *accelerator = np->accelerator;
 	struct netfront_accel_hooks *hooks;
 	int rc = 0;
 
	if (np->accel_vif_state.hooks) {
		hooks = np->accel_vif_state.hooks;

		/* Last chance to get statistics from the accelerator */
		hooks->get_stats(np->netdev, &np->stats);

		spin_unlock_irqrestore(&accelerator->vif_states_lock, 
				       *lock_flags);

 		/* 
 		 * Try and do the opposite of accelerator_probe_new_vif
 		 * to ensure there's no state pointing back at the 
 		 * netdev 
 		 */
		accelerator_remove_single_hook(accelerator, 
 					       &np->accel_vif_state);

		rc = accelerator->hooks->remove(dev);

		spin_lock_irqsave(&accelerator->vif_states_lock, *lock_flags);
	}
 
 	return rc;
}


static int netfront_remove_accelerator(struct netfront_info *np,
				       struct xenbus_device *dev)
{
	struct netfront_accelerator *accelerator;
 	struct netfront_accel_vif_state *tmp_vif_state;
  	unsigned long flags;
	int rc = 0; 

 	/* Check that we've got a device that was accelerated */
 	if (np->accelerator == NULL)
		return rc;

	accelerator = np->accelerator;

	spin_lock_irqsave(&accelerator->vif_states_lock, flags); 

	list_for_each_entry(tmp_vif_state, &accelerator->vif_states,
			    link) {
		if (tmp_vif_state == &np->accel_vif_state) {
			list_del(&np->accel_vif_state.link);
			break;
		}
	}

	rc = do_remove(np, dev, &flags);

	np->accelerator = NULL;

	spin_unlock_irqrestore(&accelerator->vif_states_lock, flags); 

	return rc;
}


int netfront_accelerator_call_remove(struct netfront_info *np,
				     struct xenbus_device *dev)
{
	int rc;
	netfront_accelerator_remove_watch(np);
	mutex_lock(&accelerator_mutex);
	rc = netfront_remove_accelerator(np, dev);
	mutex_unlock(&accelerator_mutex);
	return rc;
}

  
int netfront_accelerator_suspend(struct netfront_info *np,
 				 struct xenbus_device *dev)
{
	unsigned long flags;
	int rc = 0;
	
	netfront_accelerator_remove_watch(np);

	mutex_lock(&accelerator_mutex);

 	/* Check that we've got a device that was accelerated */
 	if (np->accelerator == NULL)
		goto out;

	/* 
	 * Call the remove accelerator hook, but leave the vif_state
	 * on the accelerator's list in case there is a suspend_cancel.
	 */
	spin_lock_irqsave(&np->accelerator->vif_states_lock, flags); 
	
	rc = do_remove(np, dev, &flags);

	spin_unlock_irqrestore(&np->accelerator->vif_states_lock, flags); 
 out:
	mutex_unlock(&accelerator_mutex);
	return rc;
}
  
  
int netfront_accelerator_suspend_cancel(struct netfront_info *np,
 					struct xenbus_device *dev)
{
	/* 
	 * Setting the watch will cause it to fire and probe the
	 * accelerator, so no need to call accelerator_probe_new_vif()
	 * directly here
	 */
	netfront_accelerator_add_watch(np);
 	return 0;
}
 
 
void netfront_accelerator_resume(struct netfront_info *np,
 				 struct xenbus_device *dev)
{
 	struct netfront_accel_vif_state *accel_vif_state = NULL;
 	spinlock_t *vif_states_lock;
 	unsigned long flags;
 
 	mutex_lock(&accelerator_mutex);

	/* Check that we've got a device that was accelerated */
 	if(np->accelerator == NULL)
		goto out;

 	/* Find the vif_state from the accelerator's list */
 	list_for_each_entry(accel_vif_state, &np->accelerator->vif_states, 
 			    link) {
 		if (accel_vif_state->dev == dev) {
 			BUG_ON(accel_vif_state != &np->accel_vif_state);
 
 			vif_states_lock = &np->accelerator->vif_states_lock;
			spin_lock_irqsave(vif_states_lock, flags); 
 
 			/* 
 			 * Remove it from the accelerator's list so
 			 * state is consistent for probing new vifs
 			 * when they get connected
 			 */
 			list_del(&accel_vif_state->link);
 			np->accelerator = NULL;
 
 			spin_unlock_irqrestore(vif_states_lock, flags); 
 			
			break;
 		}
 	}

 out:
	mutex_unlock(&accelerator_mutex);
	return;
}


int netfront_check_accelerator_queue_ready(struct net_device *dev,
					   struct netfront_info *np)
{
	struct netfront_accelerator *accelerator;
	struct netfront_accel_hooks *hooks;
	int rc = 1;
	unsigned long flags;

	accelerator = np->accelerator;

	/* Call the check_ready accelerator hook. */ 
	if (np->accel_vif_state.hooks && accelerator) {
		spin_lock_irqsave(&accelerator->vif_states_lock, flags); 
		hooks = np->accel_vif_state.hooks;
		if (hooks && np->accelerator == accelerator)
			rc = np->accel_vif_state.hooks->check_ready(dev);
		spin_unlock_irqrestore(&accelerator->vif_states_lock, flags);
	}

	return rc;
}


void netfront_accelerator_call_stop_napi_irq(struct netfront_info *np,
					     struct net_device *dev)
{
	struct netfront_accelerator *accelerator;
	struct netfront_accel_hooks *hooks;
	unsigned long flags;

	accelerator = np->accelerator;

	/* Call the stop_napi_interrupts accelerator hook. */
	if (np->accel_vif_state.hooks && accelerator != NULL) {
		spin_lock_irqsave(&accelerator->vif_states_lock, flags); 
		hooks = np->accel_vif_state.hooks;
		if (hooks && np->accelerator == accelerator)
 			np->accel_vif_state.hooks->stop_napi_irq(dev);
		spin_unlock_irqrestore(&accelerator->vif_states_lock, flags);
	}
}


int netfront_accelerator_call_get_stats(struct netfront_info *np,
					struct net_device *dev)
{
	struct netfront_accelerator *accelerator;
	struct netfront_accel_hooks *hooks;
	unsigned long flags;
	int rc = 0;

	accelerator = np->accelerator;

	/* Call the get_stats accelerator hook. */
	if (np->accel_vif_state.hooks && accelerator != NULL) {
		spin_lock_irqsave(&accelerator->vif_states_lock, flags); 
		hooks = np->accel_vif_state.hooks;
		if (hooks && np->accelerator == accelerator)
 			rc = np->accel_vif_state.hooks->get_stats(dev,
								  &np->stats);
		spin_unlock_irqrestore(&accelerator->vif_states_lock, flags);
	}
	return rc;
}

