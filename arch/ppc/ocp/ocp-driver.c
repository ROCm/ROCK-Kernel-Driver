/*
 * FILE NAME: ocp-driver.c
 *
 * BRIEF MODULE DESCRIPTION:
 * driver callback, id matching and registration
 * Based on drivers/pci/pci-driver, Copyright (c) 1997--1999 Martin Mares
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
#include <linux/module.h>
#include <linux/init.h>

/*
 *  Registration of OCP drivers and handling of hot-pluggable devices.
 */

static int
ocp_device_probe(struct device *dev)
{
	int error = 0;
	struct ocp_driver *drv;
	struct ocp_device *ocp_dev;

	drv = to_ocp_driver(dev->driver);
	ocp_dev = to_ocp_dev(dev);

	if (drv->probe) {
		error = drv->probe(ocp_dev);
		DBG("probe return code %d\n", error);
		if (error >= 0) {
			ocp_dev->driver = drv;
			error = 0;
		}
	}
	return error;
}

static int
ocp_device_remove(struct device *dev)
{
	struct ocp_device *ocp_dev = to_ocp_dev(dev);

	if (ocp_dev->driver) {
		if (ocp_dev->driver->remove)
			ocp_dev->driver->remove(ocp_dev);
		ocp_dev->driver = NULL;
	}
	return 0;
}

static int
ocp_device_suspend(struct device *dev, u32 state, u32 level)
{
	struct ocp_device *ocp_dev = to_ocp_dev(dev);

	int error = 0;

	if (ocp_dev->driver) {
		if (level == SUSPEND_SAVE_STATE && ocp_dev->driver->save_state)
			error = ocp_dev->driver->save_state(ocp_dev, state);
		else if (level == SUSPEND_POWER_DOWN
			 && ocp_dev->driver->suspend)
			error = ocp_dev->driver->suspend(ocp_dev, state);
	}
	return error;
}

static int
ocp_device_resume(struct device *dev, u32 level)
{
	struct ocp_device *ocp_dev = to_ocp_dev(dev);

	if (ocp_dev->driver) {
		if (level == RESUME_POWER_ON && ocp_dev->driver->resume)
			ocp_dev->driver->resume(ocp_dev);
	}
	return 0;
}

/**
 * ocp_bus_match - Works out whether an OCP device matches any
 * of the IDs listed for a given OCP driver.
 * @dev: the generic device struct for the OCP device
 * @drv: the generic driver struct for the OCP driver
 *
 * Used by a driver to check whether a OCP device present in the
 * system is in its list of supported devices.  Returns 1 for a
 * match, or 0 if there is no match.
 */
static int
ocp_bus_match(struct device *dev, struct device_driver *drv)
{
	struct ocp_device *ocp_dev = to_ocp_dev(dev);
	struct ocp_driver *ocp_drv = to_ocp_driver(drv);
	const struct ocp_device_id *ids = ocp_drv->id_table;

	if (!ids)
		return 0;

	while (ids->vendor || ids->device) {
		if ((ids->vendor == OCP_ANY_ID
		     || ids->vendor == ocp_dev->vendor)
		    && (ids->device == OCP_ANY_ID
			|| ids->device == ocp_dev->device)) {
			DBG("Bus match -vendor:%x device:%x\n", ids->vendor,
			    ids->device);
			return 1;
		}
		ids++;
	}
	return 0;
}

struct bus_type ocp_bus_type = {
	.name = "ocp",
	.match = ocp_bus_match,
};

static int __init
ocp_driver_init(void)
{
	return bus_register(&ocp_bus_type);
}

postcore_initcall(ocp_driver_init);

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
ocp_register_driver(struct ocp_driver *drv)
{
	int count = 0;

	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &ocp_bus_type;
	drv->driver.probe = ocp_device_probe;
	drv->driver.resume = ocp_device_resume;
	drv->driver.suspend = ocp_device_suspend;
	drv->driver.remove = ocp_device_remove;

	/* register with core */
	count = driver_register(&drv->driver);
	return count ? count : 1;
}

/**
 * ocp_unregister_driver - unregister a ocp driver
 * @drv: the driver structure to unregister
 *
 * Deletes the driver structure from the list of registered OCP drivers,
 * gives it a chance to clean up by calling its remove() function for
 * each device it was responsible for, and marks those devices as
 * driverless.
 */

void
ocp_unregister_driver(struct ocp_driver *drv)
{
	driver_unregister(&drv->driver);
}

EXPORT_SYMBOL(ocp_register_driver);
EXPORT_SYMBOL(ocp_unregister_driver);
EXPORT_SYMBOL(ocp_bus_type);
