/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

/*
 * Hardware Inventory
 *
 * See sys/sn/invent.h for an explanation of the hardware inventory contents.
 *
 */
#include <linux/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>

void
inventinit(void)
{
}

/*
 * For initializing/updating an inventory entry.
 */
void
replace_in_inventory(
	inventory_t *pinv, int class, int type,
	int controller, int unit, int state)
{
	pinv->inv_class = class;
	pinv->inv_type = type;
	pinv->inv_controller = controller;
	pinv->inv_unit = unit;
	pinv->inv_state = state;
}

/*
 * Inventory addition 
 *
 * XXX NOTE: Currently must be called after dynamic memory allocator is
 * initialized.
 *
 */
void
add_to_inventory(int class, int type, int controller, int unit, int state)
{
	(void)device_inventory_add((devfs_handle_t)GRAPH_VERTEX_NONE, class, type, 
					controller, unit, state);
}


/*
 * Inventory retrieval 
 *
 * These two routines are intended to prevent the caller from having to know
 * the internal structure of the inventory table.
 *
 */
inventory_t *
get_next_inventory(invplace_t *place)
{
	inventory_t *pinv;
	devfs_handle_t device = place->invplace_vhdl;
	int rv;

	while ((pinv = device_inventory_get_next(device, place)) == NULL) {
		/*
		 * We've exhausted inventory items on the last device.
		 * Advance to next device.
		 */
		rv = hwgraph_vertex_get_next(&device, &place->invplace_vplace);
		if (rv != LABELCL_SUCCESS)
			return(NULL);
		place->invplace_vhdl = device;
		place->invplace_inv = NULL; /* Start from beginning invent on this device */
	}

	return(pinv);
}

/* ARGSUSED */
int
get_sizeof_inventory(int abi)
{
	return sizeof(inventory_t);
}

/*
 * Hardware inventory scanner.
 *
 * Calls fun() for every entry in inventory list unless fun() returns something
 * other than 0.
 */
int
scaninvent(int (*fun)(inventory_t *, void *), void *arg)
{
	inventory_t *ie;
	invplace_t iplace = { NULL,NULL, NULL };
	int rc;

	ie = 0;
	rc = 0;
	while ( (ie = (inventory_t *)get_next_inventory(&iplace)) ) {
		rc = (*fun)(ie, arg);
		if (rc)
			break;
	}
	return rc;
}

/*
 * Find a particular inventory object
 *
 * pinv can be a pointer to an inventory entry and the search will begin from
 * there, or it can be 0 in which case the search starts at the beginning.
 * A -1 for any of the other arguments is a wildcard (i.e. it always matches).
 */
inventory_t *
find_inventory(inventory_t *pinv, int class, int type, int controller,
	       int unit, int state)
{
	invplace_t iplace =  { NULL,NULL, NULL };

	while ((pinv = (inventory_t *)get_next_inventory(&iplace)) != NULL) {
		if (class != -1 && pinv->inv_class != class)
			continue;
		if (type != -1 && pinv->inv_type != type)
			continue;

		/* XXXX - perhaps the "state" entry should be ignored so an
		 * an existing entry can be updated.  See vino_init() and
		 * ml/IP22.c:add_ioboard() for an example.
		 */
		if (state != -1 && pinv->inv_state != state)
			continue;
		if (controller != -1
		    && pinv->inv_controller != controller)
			continue;
		if (unit != -1 && pinv->inv_unit != unit)
			continue;
		break;
	}

	return(pinv);
}


/*
** Retrieve inventory data associated with a device.
*/
inventory_t *
device_inventory_get_next(	devfs_handle_t device,
				invplace_t *invplace)
{
	inventory_t *pinv;
	int rv;

	rv = hwgraph_inventory_get_next(device, invplace, &pinv);
	if (rv == LABELCL_SUCCESS)
		return(pinv);
	else
		return(NULL);
}


/*
** Associate canonical inventory information with a device (and
** add it to the general inventory).
*/
void
device_inventory_add(	devfs_handle_t device,
			int class, 
			int type, 
			major_t controller, 
			minor_t unit, 
			int state)
{
	hwgraph_inventory_add(device, class, type, controller, unit, state);
}

int
device_controller_num_get(devfs_handle_t device)
{
	return (hwgraph_controller_num_get(device));
}

void
device_controller_num_set(devfs_handle_t device, int contr_num)
{
	hwgraph_controller_num_set(device, contr_num);
}
