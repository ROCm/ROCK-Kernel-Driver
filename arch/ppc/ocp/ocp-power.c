/*
 * FILE NAME: ocp-power.c
 *
 * BRIEF MODULE DESCRIPTION: 
 * Based on drivers/pci/power, Copyright (c) 1997--1999 Martin Mares
 *
 * Maintained by: Armin <akuster@mvista.com>
 *
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <asm/ocp.h>
#include <linux/pm.h>
#include <linux/init.h>

/*
 * OCP Power management..
 *
 * This needs to be done centralized, so that we power manage OCP
 * devices in the right order: we should not shut down OCP bridges
 * before we've shut down the devices behind them, and we should
 * not wake up devices before we've woken up the bridge to the
 * device.
 *
 * We do not touch devices that don't have a driver that exports
 * a suspend/resume function. That is just too dangerous. If the default
 * OCP suspend/resume functions work for a device, the driver can
 * easily implement them (ie just have a suspend function that calls
 * the ocp_set_power_state() function).
 */

static int ocp_pm_save_state_device(struct ocp_device *dev, u32 state)
{
	int error = 0;
	if (dev) {
		struct ocp_driver *driver = dev->driver;
		if (driver && driver->save_state) 
			error = driver->save_state(dev,state);
	}
	return error;
}

static int ocp_pm_suspend_device(struct ocp_device *dev, u32 state)
{
	int error = 0;
	if (dev) {
		struct ocp_driver *driver = dev->driver;
		if (driver && driver->suspend)
			error = driver->suspend(dev,state);
	}
	return error;
}

static int ocp_pm_resume_device(struct ocp_device *dev)
{
	int error = 0;
	if (dev) {
		struct ocp_driver *driver = dev->driver;
		if (driver && driver->resume)
			error = driver->resume(dev);
	}
	return error;
}

static int ocp_pm_save_state_bus(struct ocp_bus *bus, u32 state)
{
	struct list_head *list;
	int error = 0;

	list_for_each(list, &bus->children) {
		error = ocp_pm_save_state_bus(ocp_bus_b(list),state);
		if (error) return error;
	}
	list_for_each(list, &bus->devices) {
		error = ocp_pm_save_state_device(ocp_dev_b(list),state);
		if (error) return error;
	}
	return 0;
}

static int ocp_pm_suspend_bus(struct ocp_bus *bus, u32 state)
{
	struct list_head *list;

	/* Walk the bus children list */
	list_for_each(list, &bus->children) 
		ocp_pm_suspend_bus(ocp_bus_b(list),state);

	/* Walk the device children list */
	list_for_each(list, &bus->devices)
		ocp_pm_suspend_device(ocp_dev_b(list),state);
	return 0;
}

static int ocp_pm_resume_bus(struct ocp_bus *bus)
{
	struct list_head *list;

	/* Walk the device children list */
	list_for_each(list, &bus->devices)
		ocp_pm_resume_device(ocp_dev_b(list));

	/* And then walk the bus children */
	list_for_each(list, &bus->children)
		ocp_pm_resume_bus(ocp_bus_b(list));
	return 0;
}

static int ocp_pm_save_state(u32 state)
{
	struct list_head *list;
	struct ocp_bus *bus;
	int error = 0;

	list_for_each(list, &ocp_root_buses) {
		bus = ocp_bus_b(list);
		error = ocp_pm_save_state_bus(bus,state);
		if (!error)
			error = ocp_pm_save_state_device(bus->self,state);
	}
	return error;
}

static int ocp_pm_suspend(u32 state)
{
	struct list_head *list;
	struct ocp_bus *bus;

	list_for_each(list, &ocp_root_buses) {
		bus = ocp_bus_b(list);
		ocp_pm_suspend_bus(bus,state);
		ocp_pm_suspend_device(bus->self,state);
	}
	return 0;
}

static int ocp_pm_resume(void)
{
	struct list_head *list;
	struct ocp_bus *bus;

	list_for_each(list, &ocp_root_buses) {
		bus = ocp_bus_b(list);
		ocp_pm_resume_device(bus->self);
		ocp_pm_resume_bus(bus);
	}
	return 0;
}

static int 
ocp_pm_callback(struct pm_dev *pm_device, pm_request_t rqst, void *data)
{
	int error = 0;

	switch (rqst) {
	case PM_SAVE_STATE:
		error = ocp_pm_save_state((unsigned long)data);
		break;
	case PM_SUSPEND:
		error = ocp_pm_suspend((unsigned long)data);
		break;
	case PM_RESUME:
		error = ocp_pm_resume();
		break;
	default: break;
	}
	return error;
}

static int __init ocp_pm_init(void)
{
	pm_register(PM_OCP_DEV, 0, ocp_pm_callback);
	return 0;
}

subsys_initcall(ocp_pm_init);
