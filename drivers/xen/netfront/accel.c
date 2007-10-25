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
#include <linux/kref.h>

#include <xen/xenbus.h>

#include "netfront.h"

#define DPRINTK(fmt, args...)				\
	pr_debug("netfront/accel (%s:%d) " fmt,		\
	       __FUNCTION__, __LINE__, ##args)
#define IPRINTK(fmt, args...)				\
	printk(KERN_INFO "netfront/accel: " fmt, ##args)
#define WPRINTK(fmt, args...)				\
	printk(KERN_WARNING "netfront/accel: " fmt, ##args)

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 5)
#define kref_init(x,y) kref_init(x,y)
#define kref_put(x,y)  kref_put(x)
#else
#define kref_init(x,y) kref_init(x)
#define kref_put(x,y)  kref_put(x,y)
#endif

/*
 * List of all netfront accelerator plugin modules available.  Each
 * list entry is of type struct netfront_accelerator.
 */ 
static struct list_head accelerators_list;

/*
 * Lock to protect access to accelerators_list
 */
static spinlock_t accelerators_lock;

/* Forward declaration of kref cleanup functions */
static void accel_kref_release(struct kref *ref);
static void vif_kref_release(struct kref *ref);


void netif_init_accel(void)
{
	INIT_LIST_HEAD(&accelerators_list);
	spin_lock_init(&accelerators_lock);
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

	np->accel_vif_state.ready_for_probe = 1;
	np->accel_vif_state.need_probe = NULL;
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
	if (np->accelerator == NULL) {
		np->accelerator = accelerator;
		
		list_add(&np->accel_vif_state.link, &accelerator->vif_states);
	} else {
		/* 
		 * May get here legitimately if reconnecting to the
		 * same accelerator, eg. after resume, so check that
		 * is the case
		 */
		BUG_ON(np->accelerator != accelerator);
	}
}


/*
 * Initialise the state to track an accelerator plugin module.
 */ 
static int init_accelerator(const char *frontend, 
			    struct netfront_accelerator **result)
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

	accelerator->hooks = NULL;

	accelerator->ready_for_probe = 1;
	accelerator->need_probe = NULL;

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
	/* This function must be called with the vif_state_lock held */

	DPRINTK("%p\n",vif_state);

	/*
	 * Take references to stop hooks disappearing.
	 * This persists until vif_kref gets to zero.
	 */
	kref_get(&vif_state->np->accelerator->accel_kref);
	/* This persists until vif_state->hooks are cleared */
	kref_init(&vif_state->vif_kref, vif_kref_release);

	/* Make sure there are no data path operations going on */
	netif_poll_disable(vif_state->np->netdev);
	netif_tx_lock_bh(vif_state->np->netdev);

	vif_state->hooks = vif_state->np->accelerator->hooks;

	netif_tx_unlock_bh(vif_state->np->netdev);
	netif_poll_enable(vif_state->np->netdev);
}


static void accelerator_probe_new_vif(struct netfront_info *np,
				      struct xenbus_device *dev, 
				      struct netfront_accelerator *accelerator)
{
	struct netfront_accel_hooks *hooks;
	unsigned flags;
	
	DPRINTK("\n");

	spin_lock_irqsave(&accelerator->vif_states_lock, flags);
	
	/*
	 * Include this frontend device on the accelerator's list
	 */
	add_accelerator_vif(accelerator, np);
	
	hooks = accelerator->hooks;
	
	if (hooks) {
		if (np->accel_vif_state.ready_for_probe) {
			np->accel_vif_state.ready_for_probe = 0;
			
			kref_get(&accelerator->accel_kref);
			
			spin_unlock_irqrestore(&accelerator->vif_states_lock,
					       flags);
			
			hooks->new_device(np->netdev, dev);
			
			kref_put(&accelerator->accel_kref,
				 accel_kref_release);
			/* 
			 * Hooks will get linked into vif_state by a
			 * future call by the accelerator to
			 * netfront_accelerator_ready()
			 */
			return;
		} else {
			if (np->accel_vif_state.need_probe != NULL)
				DPRINTK("Probe request on vif awaiting probe\n");
			np->accel_vif_state.need_probe = hooks;
		}
	}
		
	spin_unlock_irqrestore(&accelerator->vif_states_lock,
			       flags);
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
	int rc;
	unsigned flags;

	DPRINTK(" %s\n", frontend);

	spin_lock_irqsave(&accelerators_lock, flags);

	/* 
	 * Look at list of loaded accelerators to see if the requested
	 * one is already there 
	 */
	list_for_each_entry(accelerator, &accelerators_list, link) {
		if (match_accelerator(frontend, accelerator)) {
			spin_unlock_irqrestore(&accelerators_lock, flags);

			accelerator_probe_new_vif(np, dev, accelerator);

			return 0;
		}
	}

	/* Couldn't find it, so create a new one and load the module */
	if ((rc = init_accelerator(frontend, &accelerator)) < 0) {
		spin_unlock_irqrestore(&accelerators_lock, flags);
		return rc;
	}

	spin_unlock_irqrestore(&accelerators_lock, flags);

	/* Include this frontend device on the accelerator's list */
	spin_lock_irqsave(&accelerator->vif_states_lock, flags);
	add_accelerator_vif(accelerator, np);
	spin_unlock_irqrestore(&accelerator->vif_states_lock, flags);

	DPRINTK("requesting module %s\n", frontend);

	/* load module */
	request_module("%s", frontend);

	/*
	 * Module should now call netfront_accelerator_loaded() once
	 * it's up and running, and we can continue from there 
	 */

	return 0;
}


/*
 * Go through all the netfront vifs and see if they have requested
 * this accelerator.  Notify the accelerator plugin of the relevant
 * device if so.  Called when an accelerator plugin module is first
 * loaded and connects to netfront.
 */
static void 
accelerator_probe_vifs(struct netfront_accelerator *accelerator,
		       struct netfront_accel_hooks *hooks,
		       unsigned lock_flags)
{
	struct netfront_accel_vif_state *vif_state, *tmp;

	/* Calling function must have taken the vif_states_lock */

	DPRINTK("%p\n", accelerator);

	/* 
	 * kref_init() takes a single reference to the hooks that will
	 * persist until the accelerator hooks are removed (e.g. by
	 * accelerator module unload)
	 */
	kref_init(&accelerator->accel_kref, accel_kref_release);

	/* 
	 * Store the hooks for future calls to probe a new device, and
	 * to wire into the vif_state once the accelerator plugin is
	 * ready to accelerate each vif
	 */
	BUG_ON(hooks == NULL);
	accelerator->hooks = hooks;
	
	list_for_each_entry_safe(vif_state, tmp, &accelerator->vif_states,
				 link) {
		struct netfront_info *np = vif_state->np;

		if (vif_state->ready_for_probe) {
			vif_state->ready_for_probe = 0;
			kref_get(&accelerator->accel_kref);

			/* 
			 * drop lock before calling hook.  hooks are
			 * protected by the kref
			 */
			spin_unlock_irqrestore(&accelerator->vif_states_lock,
					       lock_flags);
			
			hooks->new_device(np->netdev, vif_state->dev);
			
			kref_put(&accelerator->accel_kref, accel_kref_release);

			/* Retake lock for next go round the loop */
			spin_lock_irqsave(&accelerator->vif_states_lock, lock_flags);
			
			/*
			 * Hooks will get linked into vif_state by a call to
			 * netfront_accelerator_ready() once accelerator
			 * plugin is ready for action
			 */
		} else {
			if (vif_state->need_probe != NULL)
				DPRINTK("Probe request on vif awaiting probe\n");
			vif_state->need_probe = hooks;
		}
	}
	
	/* Return with vif_states_lock held, as on entry */
}


/* 
 * Wrapper for accelerator_probe_vifs that checks now is a good time
 * to do the probe, and postpones till previous state cleared up if
 * necessary
 */
static void 
accelerator_probe_vifs_on_load(struct netfront_accelerator *accelerator,
			       struct netfront_accel_hooks *hooks)
{
	unsigned flags;

	DPRINTK("\n");

	spin_lock_irqsave(&accelerator->vif_states_lock, flags);
	
	if (accelerator->ready_for_probe) {
		accelerator->ready_for_probe = 0;
		accelerator_probe_vifs(accelerator, hooks, flags);
	} else {
		if (accelerator->need_probe)
			DPRINTK("Probe request on accelerator awaiting probe\n");
		accelerator->need_probe = hooks;
	}

	spin_unlock_irqrestore(&accelerator->vif_states_lock,
			       flags);
}


/* 
 * Called by the netfront accelerator plugin module when it has loaded 
 */
int netfront_accelerator_loaded(int version, const char *frontend, 
				struct netfront_accel_hooks *hooks)
{
	struct netfront_accelerator *accelerator;
	unsigned flags;

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

	spin_lock_irqsave(&accelerators_lock, flags);

	/* 
	 * Look through list of accelerators to see if it has already
	 * been requested
	 */
	list_for_each_entry(accelerator, &accelerators_list, link) {
		if (match_accelerator(frontend, accelerator)) {
			spin_unlock_irqrestore(&accelerators_lock, flags);

			accelerator_probe_vifs_on_load(accelerator, hooks);

			return 0;
		}
	}

	/*
	 * If it wasn't in the list, add it now so that when it is
	 * requested the caller will find it
	 */
	DPRINTK("Couldn't find matching accelerator (%s)\n",
		frontend);

	init_accelerator(frontend, &accelerator);

	spin_unlock_irqrestore(&accelerators_lock, flags);

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
	unsigned flags, flags1;

	DPRINTK("%s %p\n", frontend, dev);

	spin_lock_irqsave(&accelerators_lock, flags);

	list_for_each_entry(accelerator, &accelerators_list, link) {
		if (match_accelerator(frontend, accelerator)) {
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
			goto done;
		}
	}

 done:
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
	netif_poll_disable(vif_state->np->netdev);
	netif_tx_lock_bh(vif_state->np->netdev);

	/* 
	 * Remove the hooks, but leave the vif_state on the
	 * accelerator's list as that signifies this vif is
	 * interested in using that accelerator if it becomes
	 * available again
	 */
	vif_state->hooks = NULL;
	
	netif_tx_unlock_bh(vif_state->np->netdev);
	netif_poll_enable(vif_state->np->netdev);		       
}


/* 
 * Safely remove the accelerator function hooks from a netfront state.
 */
static void accelerator_remove_hooks(struct netfront_accelerator *accelerator,
				     int remove_master)
{
	struct netfront_accel_vif_state *vif_state, *tmp;
	unsigned flags;

	spin_lock_irqsave(&accelerator->vif_states_lock, flags);

	list_for_each_entry_safe(vif_state, tmp,
				 &accelerator->vif_states,
				 link) {
		accelerator_remove_single_hook(accelerator, vif_state);

		/* 
		 * Remove the reference taken when the vif_state hooks
		 * were set, must be called without lock held
		 */
		spin_unlock_irqrestore(&accelerator->vif_states_lock, flags);
		kref_put(&vif_state->vif_kref, vif_kref_release);
		spin_lock_irqsave(&accelerator->vif_states_lock, flags);
	}
	
	if(remove_master)
		accelerator->hooks = NULL;

	spin_unlock_irqrestore(&accelerator->vif_states_lock, flags);

	if(remove_master)
		/* Remove the reference taken when module loaded */ 
		kref_put(&accelerator->accel_kref, accel_kref_release);
}


/* 
 * Called by a netfront accelerator when it is unloaded.  This safely
 * removes the hooks into the plugin and blocks until all devices have
 * finished using it, so on return it is safe to unload.
 */
void netfront_accelerator_stop(const char *frontend, int unloading)
{
	struct netfront_accelerator *accelerator;
	unsigned flags;

	spin_lock_irqsave(&accelerators_lock, flags);

	list_for_each_entry(accelerator, &accelerators_list, link) {
		if (match_accelerator(frontend, accelerator)) {
			spin_unlock_irqrestore(&accelerators_lock, flags);

			/* 
			 * Use semaphore to ensure we know when all
			 * uses of hooks are complete
			 */
			sema_init(&accelerator->exit_semaphore, 0);

			accelerator_remove_hooks(accelerator, unloading);

			if (unloading)
				/* Wait for hooks to be unused, then return */
				down(&accelerator->exit_semaphore);
			
			return;
		}
	}
	spin_unlock_irqrestore(&accelerators_lock, flags);
}
EXPORT_SYMBOL_GPL(netfront_accelerator_stop);



int netfront_check_accelerator_queue_busy(struct net_device *dev,
					  struct netfront_info *np)
{
	struct netfront_accel_hooks *hooks;
	int rc = 1;
	unsigned flags;

	/*
	 * Call the check busy accelerator hook. The use count for the
	 * accelerator's hooks is incremented for the duration of the
	 * call to prevent the accelerator being able to modify the
	 * hooks in the middle (by, for example, unloading)
	 */ 
	if (np->accel_vif_state.hooks) {
		spin_lock_irqsave(&np->accelerator->vif_states_lock, flags); 
		hooks = np->accel_vif_state.hooks;
		if (hooks) {
			kref_get(&np->accel_vif_state.vif_kref);
			spin_unlock_irqrestore
				(&np->accelerator->vif_states_lock, flags);

			rc = np->accel_vif_state.hooks->check_busy(dev);
			
			kref_put(&np->accel_vif_state.vif_kref,
				 vif_kref_release);
		} else {
			spin_unlock_irqrestore
				(&np->accelerator->vif_states_lock, flags);
		}
	}

	return rc;
}


int netfront_accelerator_call_remove(struct netfront_info *np,
				     struct xenbus_device *dev)
{
	struct netfront_accelerator *accelerator = np->accelerator;
	struct netfront_accel_vif_state *tmp_vif_state;
	struct netfront_accel_hooks *hooks;
	unsigned flags;
	int rc = 0;

	/* 
	 * Call the remove accelerator hook. The use count for the
	 * accelerator's hooks is incremented for the duration of the
	 * call to prevent the accelerator being able to modify the
	 * hooks in the middle (by, for example, unloading)
	 */ 
	spin_lock_irqsave(&np->accelerator->vif_states_lock, flags); 
	hooks = np->accel_vif_state.hooks;

	/*
	 * Remove this vif_state from the accelerator's list 
	 */
	list_for_each_entry(tmp_vif_state, &accelerator->vif_states, link) {
		if (tmp_vif_state == &np->accel_vif_state) {
			list_del(&np->accel_vif_state.link);
			break;
		}
	}
	   
	if (hooks) {
		kref_get(&np->accel_vif_state.vif_kref);
		spin_unlock_irqrestore
			(&np->accelerator->vif_states_lock, flags);
		
		rc = np->accel_vif_state.hooks->remove(dev);
		
		kref_put(&np->accel_vif_state.vif_kref,
			 vif_kref_release);
		
		spin_lock_irqsave(&np->accelerator->vif_states_lock,
				  flags);

		/* 
		 * Try and do the opposite of accelerator_probe_new_vif
		 * to ensure there's no state pointing back at the 
		 * netdev 
		 */
		accelerator_remove_single_hook(accelerator, 
					       &np->accel_vif_state);
				
		/* 
		 * Remove the reference taken when the vif_state hooks
		 * were set, must be called without lock held
		 */
		spin_unlock_irqrestore(&accelerator->vif_states_lock,
				       flags);
		kref_put(&np->accel_vif_state.vif_kref,
			 vif_kref_release);
	} else {
		spin_unlock_irqrestore(&np->accelerator->vif_states_lock,
				       flags); 
	}

	return rc;
}


int netfront_accelerator_call_suspend(struct netfront_info *np,
				      struct xenbus_device *dev)
{
	struct netfront_accel_hooks *hooks;
	unsigned flags;
	int rc = 0;

	IPRINTK("netfront_accelerator_call_suspend\n");

	/* 
	 *  Call the suspend accelerator hook.  The use count for the
	 *  accelerator's hooks is incremented for the duration of
	 *  the call to prevent the accelerator being able to modify
	 *  the hooks in the middle (by, for example, unloading)
	 */
	if (np->accel_vif_state.hooks) {
		spin_lock_irqsave(&np->accelerator->vif_states_lock, flags); 
		hooks = np->accel_vif_state.hooks;
		if (hooks) {
			kref_get(&np->accel_vif_state.vif_kref);
			spin_unlock_irqrestore
				(&np->accelerator->vif_states_lock, flags);

			rc = np->accel_vif_state.hooks->suspend(dev);

			kref_put(&np->accel_vif_state.vif_kref,
				 vif_kref_release);
		} else {
			spin_unlock_irqrestore
				(&np->accelerator->vif_states_lock, flags);
		}
	}
	return rc;
}


int netfront_accelerator_call_suspend_cancel(struct netfront_info *np,
					     struct xenbus_device *dev)
{
	struct netfront_accel_hooks *hooks;
	unsigned flags;
	int rc = 0;

	IPRINTK(" netfront_accelerator_call_suspend_cancel\n");

	/* 
	 *  Call the suspend_cancel accelerator hook.  The use count
	 *  for the accelerator's hooks is incremented for the
	 *  duration of the call to prevent the accelerator being able
	 *  to modify the hooks in the middle (by, for example,
	 *  unloading)
	 */
	if (np->accel_vif_state.hooks) {
		spin_lock_irqsave(&np->accelerator->vif_states_lock, flags); 
		hooks = np->accel_vif_state.hooks;
		if (hooks) {
			kref_get(&np->accel_vif_state.vif_kref);
			spin_unlock_irqrestore
				(&np->accelerator->vif_states_lock, flags);

			rc = np->accel_vif_state.hooks->suspend_cancel(dev);

			kref_put(&np->accel_vif_state.vif_kref,
				 vif_kref_release);
		} else {
			spin_unlock_irqrestore
				(&np->accelerator->vif_states_lock, flags);
		}
	}
	return rc;
}


int netfront_accelerator_call_resume(struct netfront_info *np,
				     struct xenbus_device *dev)
{
	struct netfront_accel_hooks *hooks;
	unsigned flags;
	int rc = 0;

	/* 
	 *  Call the resume accelerator hook.  The use count for the
	 *  accelerator's hooks is incremented for the duration of
	 *  the call to prevent the accelerator being able to modify
	 *  the hooks in the middle (by, for example, unloading)
	 */
	if (np->accel_vif_state.hooks) {
		spin_lock_irqsave(&np->accelerator->vif_states_lock, flags); 
		hooks = np->accel_vif_state.hooks;
		if (hooks) {
			kref_get(&np->accel_vif_state.vif_kref);
			spin_unlock_irqrestore
				(&np->accelerator->vif_states_lock, flags);

			rc = np->accel_vif_state.hooks->resume(dev);

			kref_put(&np->accel_vif_state.vif_kref,
				 vif_kref_release);
		} else {
			spin_unlock_irqrestore
				(&np->accelerator->vif_states_lock, flags);
		}
	}
	return rc;
}


void netfront_accelerator_call_backend_changed(struct netfront_info *np,
					       struct xenbus_device *dev,
					       enum xenbus_state backend_state)
{
	struct netfront_accel_hooks *hooks;
	unsigned flags;

	/* 
	 * Call the backend_changed accelerator hook. The use count
	 * for the accelerator's hooks is incremented for the duration
	 * of the call to prevent the accelerator being able to modify
	 * the hooks in the middle (by, for example, unloading)
	 */
	if (np->accel_vif_state.hooks) {
		spin_lock_irqsave(&np->accelerator->vif_states_lock, flags); 
		hooks = np->accel_vif_state.hooks;
		if (hooks) {
			kref_get(&np->accel_vif_state.vif_kref);
			spin_unlock_irqrestore
				(&np->accelerator->vif_states_lock, flags);

 			np->accel_vif_state.hooks->backend_changed
 				(dev, backend_state);

			kref_put(&np->accel_vif_state.vif_kref,
				 vif_kref_release);
		} else {
			spin_unlock_irqrestore
				(&np->accelerator->vif_states_lock, flags);
		}
	}
}


void netfront_accelerator_call_stop_napi_irq(struct netfront_info *np,
					     struct net_device *dev)
{
	struct netfront_accel_hooks *hooks;
	unsigned flags;

	/* 
	 * Call the stop_napi_interrupts accelerator hook.  The use
	 * count for the accelerator's hooks is incremented for the
	 * duration of the call to prevent the accelerator being able
	 * to modify the hooks in the middle (by, for example,
	 * unloading)
	 */

	if (np->accel_vif_state.hooks) {
		spin_lock_irqsave(&np->accelerator->vif_states_lock, flags); 
		hooks = np->accel_vif_state.hooks;
		if (hooks) {
			kref_get(&np->accel_vif_state.vif_kref);
			spin_unlock_irqrestore
				(&np->accelerator->vif_states_lock, flags);

 			np->accel_vif_state.hooks->stop_napi_irq(dev);
		
			kref_put(&np->accel_vif_state.vif_kref,
				 vif_kref_release);
		} else {
			spin_unlock_irqrestore
				(&np->accelerator->vif_states_lock, flags);
		}
	}
}


/* 
 * Once all users of hooks have kref_put()'d we can signal that it's
 * safe to unload
 */ 
static void accel_kref_release(struct kref *ref)
{
	struct netfront_accelerator *accelerator =
		container_of(ref, struct netfront_accelerator, accel_kref);
	struct netfront_accel_hooks *hooks;
	unsigned flags;

	DPRINTK("%p\n", accelerator);

	/* Signal that all users of hooks are done */
	up(&accelerator->exit_semaphore);

	spin_lock_irqsave(&accelerator->vif_states_lock, flags);
	if (accelerator->need_probe) {
		hooks = accelerator->need_probe;
		accelerator->need_probe = NULL;
		accelerator_probe_vifs(accelerator, hooks, flags);
	} 
	else
		accelerator->ready_for_probe = 1;

	spin_unlock_irqrestore(&accelerator->vif_states_lock, flags);
}


static void vif_kref_release(struct kref *ref)
{
	struct netfront_accel_vif_state *vif_state = 
		container_of(ref, struct netfront_accel_vif_state, vif_kref);
	struct netfront_accel_hooks *hooks;
	unsigned flags;

	DPRINTK("%p\n", vif_state);

	/* 
	 * Now that this vif has finished using the hooks, it can
	 * decrement the accelerator's global copy ref count 
	 */
	kref_put(&vif_state->np->accelerator->accel_kref, accel_kref_release);

	spin_lock_irqsave(&vif_state->np->accelerator->vif_states_lock, flags);
	if (vif_state->need_probe) {
		hooks = vif_state->need_probe;
		vif_state->need_probe = NULL;
		spin_unlock_irqrestore
			(&vif_state->np->accelerator->vif_states_lock, flags);
		hooks->new_device(vif_state->np->netdev, vif_state->dev);
	} else {
		vif_state->ready_for_probe = 1;
		spin_unlock_irqrestore
			(&vif_state->np->accelerator->vif_states_lock, flags);
	}
}

