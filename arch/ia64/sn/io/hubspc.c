/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997, 2000-2002 Silicon Graphics, Inc.  All rights reserved.
 */

/*
 * hubspc.c - Hub Memory Space Management Driver
 * This driver implements the managers for the following
 * memory resources:
 * 1) reference counters
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_cpuid.h>
#include <linux/devfs_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <asm/io.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/sn1/mem_refcnt.h>
#include <asm/sn/addrs.h>
#include <asm/sn/snconfig.h>
#include <asm/sn/sn1/hubspc.h>
#include <asm/sn/ksys/elsc.h>
#include <asm/sn/simulator.h>


/* Uncomment the following line for tracing */
/* #define HUBSPC_DEBUG 1 */

int hubspc_devflag = D_MP;


/***********************************************************************/
/* CPU Prom Space 						       */
/***********************************************************************/

typedef struct cpuprom_info {
	devfs_handle_t	prom_dev;
	devfs_handle_t	nodevrtx;
	struct	cpuprom_info *next;
}cpuprom_info_t;

static cpuprom_info_t	*cpuprom_head;
static spinlock_t	cpuprom_spinlock;
#define	PROM_LOCK()	mutex_spinlock(&cpuprom_spinlock)
#define	PROM_UNLOCK(s)	mutex_spinunlock(&cpuprom_spinlock, (s))

/*
 * Add prominfo to the linked list maintained.
 */
void
prominfo_add(devfs_handle_t hub, devfs_handle_t prom)
{
	cpuprom_info_t	*info;
	unsigned long	s;

	info = kmalloc(sizeof(cpuprom_info_t), GFP_KERNEL);
	ASSERT(info);
	info->prom_dev = prom;
	info->nodevrtx = hub;


	s = PROM_LOCK();
	info->next = cpuprom_head;
	cpuprom_head = info;
	PROM_UNLOCK(s);
}

void
prominfo_del(devfs_handle_t prom)
{
	unsigned long	s;
	cpuprom_info_t	*info;
	cpuprom_info_t	**prev;

	s = PROM_LOCK();
	prev = &cpuprom_head;
	while ( (info = *prev) ) {
		if (info->prom_dev == prom) {
			*prev = info->next;
			PROM_UNLOCK(s);
			return;
		}
		
		prev = &info->next;
	}
	PROM_UNLOCK(s);
	ASSERT(0);
}

devfs_handle_t
prominfo_nodeget(devfs_handle_t prom)
{
	unsigned long	s;
	cpuprom_info_t	*info;

	s = PROM_LOCK();
	info = cpuprom_head;
	while (info) {
		if(info->prom_dev == prom) {
			PROM_UNLOCK(s);
			return info->nodevrtx;
		}
		info = info->next;
	}
	PROM_UNLOCK(s);
	return 0;
}

#if defined(CONFIG_IA64_SGI_SN1)
#define	SN_PROMVERSION		INV_IP35PROM

/* Add "detailed" labelled inventory information to the
 * prom vertex 
 */
void
cpuprom_detailed_inventory_info_add(devfs_handle_t prom_dev,devfs_handle_t node)
{
	invent_miscinfo_t 	*cpuprom_inventory_info;
	extern invent_generic_t *klhwg_invent_alloc(cnodeid_t cnode, 
						     int class, int size);
	cnodeid_t		cnode = hubdev_cnodeid_get(node);

	/* Allocate memory for the extra inventory information
	 * for the  prom
	 */
	cpuprom_inventory_info = (invent_miscinfo_t *) 
		klhwg_invent_alloc(cnode, INV_PROM, sizeof(invent_miscinfo_t));

	ASSERT(cpuprom_inventory_info);

	/* Set the enabled flag so that the hinv interprets this
	 * information
	 */
	cpuprom_inventory_info->im_gen.ig_flag = INVENT_ENABLED;
	cpuprom_inventory_info->im_type = SN_PROMVERSION;
	/* Store prom revision into inventory information */
	cpuprom_inventory_info->im_rev = IP27CONFIG.pvers_rev;
	cpuprom_inventory_info->im_version = IP27CONFIG.pvers_vers;

	/* Store this info as labelled information hanging off the
	 * prom device vertex
	 */
	hwgraph_info_add_LBL(prom_dev, INFO_LBL_DETAIL_INVENT, 
			     (arbitrary_info_t) cpuprom_inventory_info);
	/* Export this information so that user programs can get to
	 * this by using attr_get()
	 */
        hwgraph_info_export_LBL(prom_dev, INFO_LBL_DETAIL_INVENT,
				sizeof(invent_miscinfo_t));
}

#endif  /* CONFIG_IA64_SGI_SN1 */


/***********************************************************************/
/* Base Hub Space Driver                                               */
/***********************************************************************/

/*
 * hubspc_init
 * Registration of the hubspc devices with the hub manager
 */
void
hubspc_init(void)
{
        /*
         * Register with the hub manager
         */

        /* The reference counters */
#if defined(CONFIG_IA64_SGI_SN1)
        hubdev_register(mem_refcnt_attach);
#endif

#ifdef CONFIG_IA64_SGI_SN1
	/* L1 system controller link */
	if ( !IS_RUNNING_ON_SIMULATOR() ) {
		/* initialize the L1 link */
		extern void l1_init(void);
		l1_init();
	}
#endif	/* CONFIG_IA64_SGI_SN1 */
#ifdef	HUBSPC_DEBUG
	printk("hubspc_init: Completed\n");
#endif	/* HUBSPC_DEBUG */
	/* Initialize spinlocks */
	mutex_spinlock_init(&cpuprom_spinlock);
}

/* ARGSUSED */
int
hubspc_open(devfs_handle_t *devp, mode_t oflag, int otyp, cred_t *crp)
{
        return (0);
}


/* ARGSUSED */
int
hubspc_close(devfs_handle_t dev, int oflag, int otyp, cred_t *crp)
{
        return (0);
}

/* ARGSUSED */
int
hubspc_map(devfs_handle_t dev, vhandl_t *vt, off_t off, size_t len, uint prot)
{
	/*REFERENCED*/
        int errcode = 0;

	/* check validity of request */
	if( len == 0 ) {
		return -ENXIO;
        }

	return errcode;
}

/* ARGSUSED */
int
hubspc_unmap(devfs_handle_t dev, vhandl_t *vt)
{
	return (0);

}

/* ARGSUSED */
int
hubspc_ioctl(devfs_handle_t dev,
             int cmd,
             void *arg,
             int mode,
             cred_t *cred_p,
             int *rvalp)
{
	return (0);

}
