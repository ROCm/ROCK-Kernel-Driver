/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_CDL_H
#define _ASM_SN_CDL_H

#include <asm/sn/sgi.h>

/*
 *	cdl: connection/driver list
 *
 *	support code for bus infrastructure for busses
 *	that have self-identifying devices; initially
 *	constructed for xtalk, pciio and gioio modules.
 */
typedef struct cdl     *cdl_p;

/*
 *	cdl_itr_f is the type for the functions
 *	that are handled by cdl_iterate.
 */

typedef void	
cdl_iter_f		(devfs_handle_t vhdl);

/*
 *	If CDL_PRI_HI is specified in the flags
 *	parameter for cdl_add_driver, then that driver's
 *	attach routine will be called for future connect
 *	points before any (non-CDL_PRI_HI) drivers.
 *
 *	The IOC3 driver uses this facility to make sure
 *	that the ioc3_attach() function is called before
 *	the attach routines of any subdevices.
 *
 *	Drivers for bridge-based crosstalk cards that
 *	are almost but not quite generic can use it to
 *	arrange that their attach() functions get called
 *	before the generic bridge drivers, so they can
 *	leave behind "hint" structures that will
 *	properly configure the generic driver.
 */
#define	CDL_PRI_HI	0x0001

/*
 *	cdl_new: construct a new connection/driver list
 *
 *	Called once for each "kind" of bus. Returns an
 *	opaque cookie representing the particular list
 *	that will be operated on by the other calls.
 */
extern cdl_p		cdl_new(char *, char *, char *);

/*
 *	cdl_del: destroy a connection/driver list.
 *
 *	Releases all dynamically allocated resources
 *	associated with the specified list. Forgets what
 *	drivers might be involved in this kind of bus,
 *	forgets what connection points have been noticed
 *	on this kind of bus.
 */
extern void		cdl_del(cdl_p reg);

/*
 *	cdl_add_driver: register a device driver
 *
 *	Calls the driver's attach routine with all
 *	connection points on the list that have the same
 *	key information as the driver; then places the
 *	driver on the list so that any connection points
 *	discovered in the future that match the driver
 *	can be handed off to the driver's attach
 *	routine.
 *
 *	CDL_PRI_HI may be specified (see above).
 */

extern int		cdl_add_driver(cdl_p reg,
				       int key1,
				       int key2,
				       char *prefix,
				       int flags);

/*
 *	cdl_del_driver: remove a device driver
 *
 *	Calls the driver's detach routine with all
 *	connection points on the list that match the
 *	driver; then forgets about the driver. Future
 *	calls to cdl_add_connpt with connections that
 *	would match this driver no longer trigger calls
 *	to the driver's attach routine.
 *
 *	NOTE: Yes, I said CONNECTION POINTS, not
 *	verticies that the driver has been attached to
 *	with hwgraph_driver_add(); this gives the driver
 *	a chance to clean up anything it did to the
 *	connection point in its attach routine. Also,
 *	this is done whether or not the attach routine
 *	was successful.
 */
extern void		cdl_del_driver(cdl_p reg, 
				       char *prefix);

/*
 *	cdl_add_connpt: add a connection point
 *
 *	Calls the attach routines of all the drivers on
 *	the list that match this connection point, in
 *	the order that they were added to the list,
 *	except that CDL_PRI_HI drivers are called first.
 *
 *	Then the vertex is added to the list, so it can
 *	be presented to any matching drivers that may be
 *	subsequently added to the list.
 */
extern int		cdl_add_connpt(cdl_p reg,
				       int key1,
				       int key2,
				       devfs_handle_t conn);

/*
 *	cdl_del_connpt: delete a connection point
 *
 *	Calls the detach routines of all matching
 *	drivers for this connection point, in the same
 *	order that the attach routines were called; then
 *	forgets about this vertex, so drivers added in
 *	the future will not be told about it.
 *
 *	NOTE: Same caveat here about the detach calls as
 *	in the cdl_del_driver() comment above.
 */
extern void		cdl_del_connpt(cdl_p reg,
				       int key1,
				       int key2,
				       devfs_handle_t conn);

/*
 *	cdl_iterate: find all verticies in the registry
 *	corresponding to the named driver and call them
 *	with the specified function (giving the vertex
 *	as the parameter).
 */

extern void		cdl_iterate(cdl_p reg,
				    char *prefix,
				    cdl_iter_f *func);

/*
 * An INFO_LBL_ASYNC_ATTACH label is attached to a vertex, pointing to
 * an instance of async_attach_s to indicate that asynchronous
 * attachment may be applied to that device ... if the corresponding
 * driver allows it.
 */

struct async_attach_s {
	sema_t async_sema;
	int    async_count;
};
typedef struct async_attach_s *async_attach_t;

async_attach_t	async_attach_new(void);
void		async_attach_free(async_attach_t);
async_attach_t  async_attach_get_info(devfs_handle_t);
void            async_attach_add_info(devfs_handle_t, async_attach_t);
void            async_attach_del_info(devfs_handle_t);
void		async_attach_signal_start(async_attach_t);
void		async_attach_signal_done(async_attach_t);
void		async_attach_waitall(async_attach_t);

#endif	/* _ASM_SN_CDL_H */
