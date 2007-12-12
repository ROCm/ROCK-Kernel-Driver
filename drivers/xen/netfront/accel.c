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

#include <xen/xenbus.h>

#include "netfront.h"

#define DPRINTK(fmt, args...)				\
	pr_debug("netfront/accel (%s:%d) " fmt,		\
	       __FUNCTION__, __LINE__, ##args)
#define IPRINTK(fmt, args...)				\
	printk(KERN_INFO "netfront/accel: " fmt, ##args)
#define WPRINTK(fmt, args...)				\
	printk(KERN_WARNING "netfront/accel: " fmt, ##args)

/*
 * List of all netfront accelerator plugin modules available.  Each
 * list entry is of type struct netfront_accelerator.
 */ 
static struct list_head accelerators_list;

/* Lock to protect access to accelerators_list */
static spinlock_t accelerators_lock;

/* Mutex to prevent concurrent loads and suspends, etc. */
DEFINE_MUTEX(accelerator_mutex);

void netif_init_accel(void)
{
	INIT_LIST_HEAD(&accelerators_list);
	spin_lock_init(&accelerators_lock);
}

void netif_exit_accel(void)
{
	struct netfront_accelerator *accelerator, *tmp;
	unsigned long flags;

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
 * Initialise the accel_vif_state field in the netfront state
 */ 
void init_accelerator_vif(struct netfront_info *np,
			  struct xenbus_device *dev)
{
	np->accelerator = NULL;

	/* It's assumed that these things don't change */
	np->accel_vif_state.np = np;
	np->accel_vif_state.dev = dev;
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

	list_add(&accelerator->link, &accelerators_list);

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

	DPRINTK("\n");

	/* Include this frontend device on the accelerator's list */
	add_accelerator_vif(accelerator, np);
	
	hooks = accelerator->hooks;
	
	if (hooks) {
		hooks->new_device(np->netdev, dev);
		/* 
		 * Hooks will get linked into vif_state by a future
		 * call by the accelerator to netfront_accelerator_ready()
		 */
	}

	return;
}

/*  
 * Request that a particular netfront accelerator plugin is loaded.
 * Usually called as a result of the vif configuration specifying
 * which one to use.
 */
int netfront_load_accelerator(struct netfront_info *np, 
			      struct xenbus_device *dev, 
			      const char *frontend)
{
	struct netfront_accelerator *accelerator;
	int rc = 0;
	unsigned long flags;

	DPRINTK(" %s\n", frontend);

	mutex_lock(&accelerator_mutex);

	spin_lock_irqsave(&accelerators_lock, flags);

	/* 
	 * Look at list of loaded accelerators to see if the requested
	 * one is already there 
	 */
	list_for_each_entry(accelerator, &accelerators_list, link) {
		if (match_accelerator(frontend, accelerator)) {
			spin_unlock_irqrestore(&accelerators_lock, flags);

			accelerator_probe_new_vif(np, dev, accelerator);

			mutex_unlock(&accelerator_mutex);
			return 0;
		}
	}

	/* Couldn't find it, so create a new one and load the module */
	if ((rc = init_accelerator(frontend, &accelerator, NULL)) < 0) {
		spin_unlock_irqrestore(&accelerators_lock, flags);
		mutex_unlock(&accelerator_mutex);
		return rc;
	}

	spin_unlock_irqrestore(&accelerators_lock, flags);

	/* Include this frontend device on the accelerator's list */
	add_accelerator_vif(accelerator, np);

	mutex_unlock(&accelerator_mutex);

	DPRINTK("requesting module %s\n", frontend);

	/* load module */
	request_module("%s", frontend);

	/*
	 * Module should now call netfront_accelerator_loaded() once
	 * it's up and running, and we can continue from there 
	 */

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
		
		hooks->new_device(np->netdev, vif_state->dev);
		
		/*
		 * Hooks will get linked into vif_state by a call to
		 * netfront_accelerator_ready() once accelerator
		 * plugin is ready for action
		 */
	}
}


/* 
 * Called by the netfront accelerator plugin module when it has loaded 
 */
int netfront_accelerator_loaded(int version, const char *frontend, 
				struct netfront_accel_hooks *hooks)
{
	struct netfront_accelerator *accelerator;
	unsigned long flags;

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

	spin_lock_irqsave(&accelerators_lock, flags);

	/* 
	 * Look through list of accelerators to see if it has already
	 * been requested
	 */
	list_for_each_entry(accelerator, &accelerators_list, link) {
		if (match_accelerator(frontend, accelerator)) {
			spin_unlock_irqrestore(&accelerators_lock, flags);

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

	spin_unlock_irqrestore(&accelerators_lock, flags);

 out:
	mutex_unlock(&accelerator_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(netfront_accelerator_loaded);


/* 
 * Called by the accelerator module after it has been probed with a
 * network device to say that it is ready to start accelerating
 * traffic on that device
 */
void netfront_accelerator_ready(const char *frontend,
				struct xenbus_device *dev)
{
	struct netfront_accelerator *accelerator;
	struct netfront_accel_vif_state *accel_vif_state;
	unsigned long flags, flags1;

	DPRINTK("%s %p\n", frontend, dev);

	spin_lock_irqsave(&accelerators_lock, flags);

	list_for_each_entry(accelerator, &accelerators_list, link) {
		if (match_accelerator(frontend, accelerator)) {
			/* 
			 * Mutex not held so need vif_states_lock for
			 * list
			 */
			spin_lock_irqsave
				(&accelerator->vif_states_lock, flags1);

			list_for_each_entry(accel_vif_state,
					    &accelerator->vif_states, link) {
				if (accel_vif_state->dev == dev)
					accelerator_set_vif_state_hooks
						(accel_vif_state);
			}

			spin_unlock_irqrestore
				(&accelerator->vif_states_lock, flags1);
			break;
		}
	}
	spin_unlock_irqrestore(&accelerators_lock, flags);
}
EXPORT_SYMBOL_GPL(netfront_accelerator_ready);


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
			accelerator_remove_single_hook(accelerator, vif_state);
			
			/* Last chance to get statistics from the accelerator */
			hooks->get_stats(vif_state->np->netdev,
					 &vif_state->np->stats);
		}

		spin_unlock_irqrestore(&accelerator->vif_states_lock, flags);

		accelerator->hooks->remove(vif_state->dev);
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

 		/* 
 		 * Try and do the opposite of accelerator_probe_new_vif
 		 * to ensure there's no state pointing back at the 
 		 * netdev 
 		 */
		accelerator_remove_single_hook(accelerator, 
 					       &np->accel_vif_state);

		/* Last chance to get statistics from the accelerator */
		hooks->get_stats(np->netdev, &np->stats);
	}

	if (accelerator->hooks) {
		spin_unlock_irqrestore(&accelerator->vif_states_lock, 
				       *lock_flags);

		rc = accelerator->hooks->remove(dev);

		spin_lock_irqsave(&accelerator->vif_states_lock, *lock_flags);
	}
 
 	return rc;
}
 
  
int netfront_accelerator_call_remove(struct netfront_info *np,
 				     struct xenbus_device *dev)
{
	struct netfront_accelerator *accelerator;
 	struct netfront_accel_vif_state *tmp_vif_state;
  	unsigned long flags;
	int rc = 0; 

	mutex_lock(&accelerator_mutex);

 	/* Check that we've got a device that was accelerated */
 	if (np->accelerator == NULL)
		goto out;

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
 out:
	mutex_unlock(&accelerator_mutex);
	return rc;
}
  
  
int netfront_accelerator_suspend(struct netfront_info *np,
 				 struct xenbus_device *dev)
{
	unsigned long flags;
	int rc = 0;

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
 	struct netfront_accel_vif_state *accel_vif_state = NULL;
 
	mutex_lock(&accelerator_mutex);

 	/* Check that we've got a device that was accelerated */
 	if (np->accelerator == NULL)
		goto out;

 	/* Find the vif_state from the accelerator's list */
 	list_for_each_entry(accel_vif_state, &np->accelerator->vif_states,
 			    link) {
 		if (accel_vif_state->dev == dev) {
 			BUG_ON(accel_vif_state != &np->accel_vif_state);
 
 			/*
 			 * Kick things off again to restore
 			 * acceleration as it was before suspend 
 			 */
 			accelerator_probe_new_vif(np, dev, np->accelerator);
 
			break;
 		}
 	}
 	
 out:
	mutex_unlock(&accelerator_mutex);
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

