/*
 * suspend.c - Functions for putting devices to sleep. 
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Labs
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include "power.h"
 
extern int sysdev_save(u32 state);
extern int sysdev_suspend(u32 state);

/*
 * The entries in the dpm_active list are in a depth first order, simply
 * because children are guaranteed to be discovered after parents, and 
 * are inserted at the back of the list on discovery. 
 * 
 * All list on the suspend path are done in reverse order, so we operate
 * on the leaves of the device tree (or forests, depending on how you want
 * to look at it ;) first. As nodes are removed from the back of the list, 
 * they are inserted into the front of their destintation lists. 
 *
 * Things are the reverse on the resume path - iterations are done in
 * forward order, and nodes are inserted at the back of their destination
 * lists. This way, the ancestors will be accessed before their descendents.
 */


/**
 *	suspend_device - Save state of one device.
 *	@dev:	Device.
 *	@state:	Power state device is entering.
 */

static int suspend_device(struct device * dev, u32 state)
{
	struct device_driver * drv = dev->driver;

	if (drv && drv->suspend)
		return drv->suspend(dev,state,SUSPEND_SAVE_STATE);
	return 0;
}


/**
 *	device_pm_suspend - Save state and stop all devices in system.
 *	@state:		Power state to put each device in. 
 *
 *	Walk the dpm_active list, call ->suspend() for each device, and move
 *	it to dpm_suspended. If we hit a failure with any of the devices, call
 *	dpm_resume() above to bring the suspended devices back to life. 
 *
 *	Have system devices save state last. 
 *
 *	Note this function leaves dpm_sem held to
 *	a) block other devices from registering.
 *	b) prevent other PM operations from happening after we've begun.
 *	c) make sure we're exclusive when we disable interrupts.
 *
 *	device_pm_resume() will release dpm_sem after restoring state to 
 *	all devices (as will this on error). You must call it once you've 
 *	called device_pm_suspend().
 */

int device_pm_suspend(u32 state)
{
	int error = 0;

	down(&dpm_sem);
	while(!list_empty(&dpm_active)) {
		struct list_head * entry = dpm_active.prev;
		struct device * dev = to_device(entry);
		list_del_init(entry);
		error = suspend_device(dev,state);

		if (!error)
			list_add(entry,&dpm_suspended);
		else {
			list_add_tail(entry,&dpm_active);
			goto Error;
		}
	}

	if ((error = sysdev_save(state)))
		goto Error;
 Done:
	return error;
 Error:
	dpm_resume();
	up(&dpm_sem);
	goto Done;
}


/**
 *	power_down_device - Put one device in low power state.
 *	@dev:	Device.
 *	@state:	Power state to enter.
 */

static int power_down_device(struct device * dev, u32 state)
{
	struct device_driver * drv = dev->driver;
	if (drv && drv->suspend)
		return drv->suspend(dev,state,SUSPEND_POWER_DOWN);
	return 0;
}


/**
 *	dpm_power_down - Put all devices in low power state.
 *	@state:	Power state to enter.
 *
 *	Walk the dpm_suspended list (with interrupts enabled) and try
 *	to power down each each. If any fail with -EAGAIN, they require
 *	the call to be done with interrupts disabled. So, we move them to
 *	the dpm_off_irq list.
 *
 *	If the call succeeds, we move each device to the dpm_off list.
 */

static int dpm_power_down(u32 state)
{
	while(!list_empty(&dpm_suspended)) {
		struct list_head * entry = dpm_suspended.prev;
		int error;

		list_del_init(entry);
		error = power_down_device(to_device(entry),state);
		if (!error)
			list_add(entry,&dpm_off);
		else if (error == -EAGAIN)
			list_add(entry,&dpm_off_irq);
		else {
			list_add_tail(entry,&dpm_suspended);
			return error;
		}
	}
	return 0;
}


/**
 *	dpm_power_down_irq - Power down devices without interrupts.
 *	@state:	State to enter.
 *
 *	Walk the dpm_off_irq list (built by dpm_power_down) and power
 *	down each device that requires the call to be made with interrupts
 *	disabled. 
 */

static int dpm_power_down_irq(u32 state)
{
	struct device * dev;
	int error = 0;

	list_for_each_entry_reverse(dev,&dpm_off_irq,power.entry) {
		if ((error = power_down_device(dev,state)))
			break;
	}
	return error;
}


/**
 *	device_pm_power_down - Put all devices in low power state.
 *	@state:		Power state to enter.
 *
 *	Walk the dpm_suspended list, calling ->power_down() for each device.
 *	Check the return value for each. If it returns 0, then we move the
 *	the device to the dpm_off list. If it returns -EAGAIN, we move it to 
 *	the dpm_off_irq list. If we get a different error, try and back out. 
 *
 *	dpm_irq_off is for devices that require interrupts to be disabled to
 *	either to power down the device or power it back on. 
 *
 *	When we're done, we disable interrrupts (!!) and walk the dpm_off_irq
 *	list to shut down the devices that need interrupts disabled. 
 *
 *	This function leaves interrupts disabled on exit, since powering down
 *	devices should be the very last thing before the system is put into a 
 *	low-power state. 
 *
 *	device_pm_power_on() should be called to re-enable interrupts and power
 *	the devices back on. 
 */

int device_pm_power_down(u32 state)
{
	int error = 0;

	if ((error = dpm_power_down(state)))
		goto ErrorIRQOn;
	local_irq_disable();
	if ((error = dpm_power_down_irq(state)))
		goto ErrorIRQOff;

	sysdev_suspend(state);
 Done:
	return error;

 ErrorIRQOff:
	dpm_power_up_irq();
	local_irq_enable();
 ErrorIRQOn:
	dpm_power_up();
	goto Done;
}


/**
 * device_suspend - suspend all devices on the device ree
 * @state:	state we're entering
 * @level:	Stage of suspend sequence we're in.
 *
 *
 *	This function is deprecated. Calls should be replaced with
 *	appropriate calls to device_pm_suspend() and device_pm_power_down().
 */

int device_suspend(u32 state, u32 level)
{

	printk("%s Called from:\n",__FUNCTION__);
	dump_stack();
	return -EFAULT;
}

