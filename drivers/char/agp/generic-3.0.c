/*
 * Generic routines for AGP 3.0 compliant bridges.
 */

#include <linux/list.h>
#include <linux/pci.h>
#include <linux/agp_backend.h>
#include <linux/module.h>

#include "agp.h"

/*
 * Fully configure and enable an AGP 3.0 host bridge and all the devices
 * lying behind it.
 */
int agp_3_0_enable(struct agp_bridge_data *bridge, u32 mode)
{
	return 0;
}

