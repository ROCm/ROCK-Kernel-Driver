/*
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * This an attempt to get Power Management going for the IBM 4xx processor.
 * This was derived from the ppc4xx._setup.c file
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <asm/ibm4xx.h>
#include <asm/ibm_ocp.h>
#include <linux/interrupt.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif


#ifdef CONFIG_APM
/*
 * OCP Power management..
 *
 * This needs to be done centralized, so that we power manage PCI
 * devices in the right order: we should not shut down PCI bridges
 * before we've shut down the devices behind them, and we should
 * not wake up devices before we've woken up the bridge to the
 * device.. Eh?
 *
 * We do not touch devices that don't have a driver that exports
 * a suspend/resume function. That is just too dangerous. If the default
 * PCI suspend/resume functions work for a device, the driver can
 * easily implement them (ie just have a suspend function that calls
 * the pci_set_power_state() function).
 */

static int ocp_pm_save_state_device(struct ocp_dev *dev, u32 state)
{
	int error = 0;
	if (dev) {
		struct ocp_dev *driver = dev->driver;
		if (driver && driver->save_state)
			error = driver->save_state(dev,state);
	}
	return error;
}

static int ocp_pm_suspend_device(struct ocp_dev *dev, u32 state)
{
	int error = 0;
	if (dev) {
		struct ocp_dev *driver = dev->driver;
		if (driver && driver->suspend)
			error = driver->suspend(dev,state);
	}
	return error;
}

static int ocp_pm_resume_device(struct ocp_dev *dev)
{
	int error = 0;
	if (dev) {
		struct ocp_dev *driver = dev->driver;
		if (driver && driver->resume)
			error = driver->resume(dev);
	}
	return error;
}

static int
ocp_pm_callback(struct pm_dev *pm_device, pm_request_t rqst, void *data)
{
	int error = 0;

	switch (rqst) {
	case PM_SAVE_STATE:
		error = ocp_pm_save_state_device((u32)data);
		break;
	case PM_SUSPEND:
		error = ocp_pm_suspend_device((u32)data);
		break;
	case PM_RESUME:
		error = ocp_pm_resume_device((u32)data);
		break;
	default: break;
	}
	return error;
}
/**
 * ocp_register_driver - register a new ocp driver
 * @drv: the driver structure to register
 *
 * Adds the driver structure to the list of registered drivers
 * Returns the number of ocp devices which were claimed by the driver
 * during registration.  The driver remains registered even if the
 * return value is zero.
 */
int
ocp_register_driver(struct ocp_dev *drv)
{
	struct ocp_dev *dev;
	struct ocp_
	list_add_tail(&drv->node, &ocp_devs);
	return 0;
}

EXPORT_SYMBOL(ocp_register_driver);
#endif

/* When bits are "1" then the given clock is
 * stopped therefore saving power 
 *
 * The objected is to turn off all unneccessary 
 * clocks and have the drivers enable/disable
 * them when in use.  We set the default
 * in the <core>.h file
 */

void __init
ppc4xx_pm_init(void)
{
	
	mtdcr(DCRN_CPMFR, 0);

	/* turn off unused hardware to save power */

	printk(KERN_INFO "OCP 4xx power management enabled\n");
	mtdcr(DCRN_CPMFR, DFLT_IBM4xx_PM);

#ifdef CONFIG_APM
	pm_gpio = pm_register(PM_SYS_DEV, 0, ocp_pm_callback);
#endif
}
__initcall(ppc4xx_pm_init);

/* Force/unforce power down for CPM Class 1 devices */

void
ppc4xx_cpm_fr(u32 bits, int val)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	if (val)
		mtdcr(DCRN_CPMFR, mfdcr(DCRN_CPMFR) | bits);
	else
		mtdcr(DCRN_CPMFR, mfdcr(DCRN_CPMFR) & ~bits);

	restore_flags(flags);
}

