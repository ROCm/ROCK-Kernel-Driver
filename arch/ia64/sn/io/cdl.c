/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#include <linux/types.h>
#include <asm/sn/sgi.h>
#include <asm/io.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/pci/bridge.h>
#include "asm/sn/ioerror_handling.h"
#include <asm/sn/xtalk/xbow.h>

#ifdef BRINGUP
/* these get called directly in cdl_add_connpt in fops bypass hack */
extern int pcibr_attach(devfs_handle_t);
extern int xbow_attach(devfs_handle_t);
#endif /* BRINGUP */

/*
 *    cdl: Connection and Driver List
 *
 *	We are not porting this to Linux.  Devices are registered via 
 *	the normal Linux PCI layer.  This is a very simplified version 
 *	of cdl that will allow us to register and call our very own 
 *	IO Infrastructure Drivers e.g. pcibr.
 */

struct cdl {
    int		part_num;
    int		mfg_num;
    int (*attach) (devfs_handle_t);
} dummy_reg;

typedef struct cdl     *cdl_p;

#define MAX_SGI_IO_INFRA_DRVR 4
struct cdl sgi_infrastructure_drivers[MAX_SGI_IO_INFRA_DRVR] =
{
	{ XBRIDGE_WIDGET_PART_NUM, XBRIDGE_WIDGET_MFGR_NUM, pcibr_attach /* &pcibr_fops  */},
	{ BRIDGE_WIDGET_PART_NUM,  BRIDGE_WIDGET_MFGR_NUM,  pcibr_attach /* &pcibr_fops */},
	{ XXBOW_WIDGET_PART_NUM,   XXBOW_WIDGET_MFGR_NUM,   xbow_attach /* &xbow_fops */},
	{ XBOW_WIDGET_PART_NUM,    XBOW_WIDGET_MFGR_NUM,    xbow_attach /* &xbow_fops */},
};

/*
 * cdl_new:  Called by pciio and xtalk.
 */
cdl_p
cdl_new(char *name, char *k1str, char *k2str)
{
    /*
     * Just return a dummy pointer.
     */
    return((cdl_p)&dummy_reg);
}

/*
 * cdl_del: Do nothing.
 */
void
cdl_del(cdl_p reg)
{
	printk("SGI IO INFRASTRUCTURE - cdl_del not supported.\n");
}

/*
 * cdl_add_driver: The driver part number and manufacturers number 
 * are statically initialized above.
 * 
  Do nothing.
 */
int
cdl_add_driver(cdl_p reg, int key1, int key2, char *prefix, int flags)
{
    return 0;
}

/*
 * cdl_del_driver: Not supported.
 */
void
cdl_del_driver(cdl_p reg,
	       char *prefix)
{

	printk("SGI IO INFRASTRUCTURE - cdl_del_driver not supported.\n");
}

/*
 * cdl_add_connpt: We found a device and it's connect point.  Call the 
 * attach routine of that driver.
 *
 * May need support for pciba registration here ...
 *
 * This routine use to create /hw/.id/pci/.../.. that links to 
 * /hw/module/006c06/Pbrick/xtalk/15/pci/<slotnum> .. do we still need 
 * it?  The specified driver attach routine does not reference these 
 * vertices.
 */
int
cdl_add_connpt(cdl_p reg, int part_num, int mfg_num, 
	       devfs_handle_t connpt)
{
	int i;
	
	/*
	 * Find the driver entry point and call the attach routine.
	 */
	for (i = 0; i < MAX_SGI_IO_INFRA_DRVR; i++) {

		if ( (part_num == sgi_infrastructure_drivers[i].part_num) &&
		   ( mfg_num == sgi_infrastructure_drivers[i].mfg_num) ) {
			/*
			 * Call the device attach routines.
			 */
			if (sgi_infrastructure_drivers[i].attach) {
			    return(sgi_infrastructure_drivers[i].attach(connpt));
			}
#ifdef BRINGUP
			/*
			 * XXX HACK ALERT bypassing fops for now..
			 */
			else {
			    printk("cdl_add_connpt: NEED FOPS FOR OUR DRIVERS!!\n");
			    printk("cdl_add_connpt: part_num= 0x%x  mfg_num= 0x%x\n",
				part_num, mfg_num);
			    return(-1);
			}
#endif /* BRINGUP */
		} else {
			continue;
		}

		printk("**** cdl_add_connpt: driver not found for part_num %d mfg_num %d ****\n", part_num, mfg_num);

		return(-1);
	}	
	if ( (i == MAX_SGI_IO_INFRA_DRVR) ) 
		printk("**** cdl_add_connpt: Driver not found for part_num 0x%x mfg_num 0x%x ****\n", part_num, mfg_num);

	return (0);
}

/*
 * cdl_del_connpt: Not implemented.
 */
void
cdl_del_connpt(cdl_p reg, int key1, int key2, devfs_handle_t connpt)
{

	printk("SGI IO INFRASTRUCTURE - cdl_del_cdl_del_connpt not supported.\n");
}

/*
 *    cdl_iterate: Not Implemented.
 */
void
cdl_iterate(cdl_p reg,
	    char *prefix,
	    cdl_iter_f * func)
{

	printk("SGI IO INFRASTRUCTURE - cdl_iterate not supported.\n");
}

async_attach_t 
async_attach_new(void)
{

	printk("SGI IO INFRASTRUCTURE - async_attach_new not supported.\n");
	return(0);
}

void 
async_attach_free(async_attach_t aa)
{
	printk("SGI IO INFRASTRUCTURE - async_attach_free not supported.\n");
}

async_attach_t 
async_attach_get_info(devfs_handle_t vhdl)
{

	printk("SGI IO INFRASTRUCTURE - async_attach_get_info not supported.\n");
	return(0);
}

void            
async_attach_add_info(devfs_handle_t vhdl, async_attach_t aa)
{
	printk("SGI IO INFRASTRUCTURE - async_attach_add_info not supported.\n");

}

void            
async_attach_del_info(devfs_handle_t vhdl)
{

	printk("SGI IO INFRASTRUCTURE - async_attach_del_info not supported.\n");

}

void async_attach_signal_start(async_attach_t aa)
{

	printk("SGI IO INFRASTRUCTURE - async_attach_signal_start not supported.\n");

}

void async_attach_signal_done(async_attach_t aa)
{

	printk("SGI IO INFRASTRUCTURE - async_attach_signal_done not supported.\n");

}

void async_attach_waitall(async_attach_t aa)
{

	printk("SGI IO INFRASTRUCTURE - async_attach_waitall not supported.\n");

}

