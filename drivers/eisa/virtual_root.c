/*
 * Virtual EISA root driver.
 * Acts as a placeholder if we don't have a proper EISA bridge.
 *
 * (C) 2003 Marc Zyngier <maz@wild-wind.fr.eu.org>
 *
 * This code is released under the GPL version 2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/eisa.h>
#include <linux/module.h>
#include <linux/init.h>

/* The default EISA device parent (virtual root device).
 * Now use a platform device, since that's the obvious choice. */

static struct platform_device eisa_root_dev = {
	.name = "eisa",
	.id   = 0,
	.dev  = {
		.name = "Virtual EISA Bridge",
	},
};

static struct eisa_root_device eisa_bus_root = {
	.dev           = &eisa_root_dev.dev,
	.bus_base_addr = 0,
	.res	       = &ioport_resource,
	.slots	       = EISA_MAX_SLOTS,
};

static int virtual_eisa_root_init (void)
{
	int r;
	
        if ((r = platform_device_register (&eisa_root_dev))) {
                return r;
        }

	eisa_root_dev.dev.driver_data = &eisa_bus_root;

	if (eisa_root_register (&eisa_bus_root)) {
		/* A real bridge may have been registered before
		 * us. So quietly unregister. */
		platform_device_unregister (&eisa_root_dev);
		return -1;
	}

	return 0;
}

device_initcall (virtual_eisa_root_init);
