/* $Id: hub_intr.c,v 1.1 2002/02/28 17:31:25 marcelo Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997, 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/sn/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/driver.h>
#include <asm/sn/iograph.h>
#include <asm/param.h>
#include <asm/sn/pio.h>
#include <asm/sn/xtalk/xwidget.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/addrs.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/intr.h>
#include <asm/sn/xtalk/xtalkaddrs.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn_cpuid.h>

extern xtalk_provider_t hub_provider;

/* ARGSUSED */
void
hub_intr_init(devfs_handle_t hubv)
{
}

/*
 * hub_device_desc_update
 *	Update the passed in device descriptor with the actual the
 * 	target cpu number and interrupt priority level.
 *	NOTE : These might be the same as the ones passed in thru
 *	the descriptor.
 */
static void
hub_device_desc_update(device_desc_t 	dev_desc, 
		       ilvl_t 		intr_swlevel,
		       cpuid_t		cpu)
{
}

int allocate_my_bit = INTRCONNECT_ANYBIT;

/*
 * Allocate resources required for an interrupt as specified in dev_desc.
 * Returns a hub interrupt handle on success, or 0 on failure.
 */
static hub_intr_t
do_hub_intr_alloc(devfs_handle_t dev,		/* which crosstalk device */
		  device_desc_t dev_desc,	/* device descriptor */
		  devfs_handle_t owner_dev,	/* owner of this interrupt, if known */
		  int uncond_nothread)		/* unconditionally non-threaded */
{
	cpuid_t cpu = (cpuid_t)0;			/* cpu to receive interrupt */
        int cpupicked = 0;
	int bit;			/* interrupt vector */
	/*REFERENCED*/
	int intr_resflags = 0;
	hub_intr_t intr_hdl;
	cnodeid_t nodeid;		/* node to receive interrupt */
	/*REFERENCED*/
	nasid_t nasid;			/* nasid to receive interrupt */
	struct xtalk_intr_s *xtalk_info;
	iopaddr_t xtalk_addr;		/* xtalk addr on hub to set intr */
	xwidget_info_t xwidget_info;	/* standard crosstalk widget info handle */
	char *intr_name = NULL;
	ilvl_t intr_swlevel = (ilvl_t)0;
	extern int default_intr_pri;
	extern void synergy_intr_alloc(int, int);


	if (dev_desc) {
		if (dev_desc->flags & D_INTR_ISERR) {
			intr_resflags = II_ERRORINT;
		} else if (!uncond_nothread && !(dev_desc->flags & D_INTR_NOTHREAD)) {
			intr_resflags = II_THREADED;
		} else {
			/* Neither an error nor a thread. */
			intr_resflags = 0;
		}
	} else {
		intr_swlevel = default_intr_pri;
		if (!uncond_nothread)
			intr_resflags = II_THREADED;
	}

	/* XXX - Need to determine if the interrupt should be threaded. */

	/* If the cpu has not been picked already then choose a candidate 
	 * interrupt target and reserve the interrupt bit 
	 */
	if (!cpupicked) {
		cpu = intr_heuristic(dev,dev_desc,allocate_my_bit,
				     intr_resflags,owner_dev,
				     intr_name,&bit);
	}

	/* At this point we SHOULD have a valid cpu */
	if (cpu == CPU_NONE) {
#if defined(SUPPORT_PRINTING_V_FORMAT)
		printk(KERN_WARNING  "%v hub_intr_alloc could not allocate interrupt\n",
			owner_dev);
#else
		printk(KERN_WARNING  "%p hub_intr_alloc could not allocate interrupt\n",
			(void *)owner_dev);
#endif
		return(0);

	}

	/* If the cpu has been picked already (due to the bridge data 
	 * corruption bug) then try to reserve an interrupt bit .
	 */
	if (cpupicked) {
		bit = intr_reserve_level(cpu, allocate_my_bit, 
					 intr_resflags, 
					 owner_dev, intr_name);
		if (bit < 0) {
#if defined(SUPPORT_PRINTING_V_FORMAT)
			printk(KERN_WARNING  "Could not reserve an interrupt bit for cpu "
				" %d and dev %v\n",
				cpu,owner_dev);
#else
			printk(KERN_WARNING  "Could not reserve an interrupt bit for cpu "
				" %d and dev %p\n",
				(int)cpu, (void *)owner_dev);
#endif
				
			return(0);
		}
	}

	nodeid = cpuid_to_cnodeid(cpu);
	nasid = cpuid_to_nasid(cpu);
	xtalk_addr = HUBREG_AS_XTALKADDR(nasid, PIREG(PI_INT_PEND_MOD, cpuid_to_subnode(cpu)));

	/*
	 * Allocate an interrupt handle, and fill it in.  There are two
	 * pieces to an interrupt handle: the piece needed by generic
	 * xtalk code which is used by crosstalk device drivers, and
	 * the piece needed by low-level IP27 hardware code.
	 */
	intr_hdl = snia_kmem_alloc_node(sizeof(struct hub_intr_s), KM_NOSLEEP, nodeid);
	ASSERT_ALWAYS(intr_hdl);

	/* 
	 * Fill in xtalk information for generic xtalk interfaces that
	 * operate on xtalk_intr_hdl's.
	 */
	xtalk_info = &intr_hdl->i_xtalk_info;
	xtalk_info->xi_dev = dev;
	xtalk_info->xi_vector = bit;
	xtalk_info->xi_addr = xtalk_addr;

	/*
	 * Regardless of which CPU we ultimately interrupt, a given crosstalk
	 * widget always handles interrupts (and PIO and DMA) through its 
	 * designated "master" crosstalk provider.
	 */
	xwidget_info = xwidget_info_get(dev);
	if (xwidget_info)
		xtalk_info->xi_target = xwidget_info_masterid_get(xwidget_info);

	/* Fill in low level hub information for hub_* interrupt interface */
	intr_hdl->i_swlevel = intr_swlevel;
	intr_hdl->i_cpuid = cpu;
	intr_hdl->i_bit = bit;
	intr_hdl->i_flags = HUB_INTR_IS_ALLOCED;

	/* Store the actual interrupt priority level & interrupt target
	 * cpu back in the device descriptor.
	 */
	hub_device_desc_update(dev_desc, intr_swlevel, cpu);
	synergy_intr_alloc((int)bit, (int)cpu);
	return(intr_hdl);
}

/*
 * Allocate resources required for an interrupt as specified in dev_desc.
 * Returns a hub interrupt handle on success, or 0 on failure.
 */
hub_intr_t
hub_intr_alloc(	devfs_handle_t dev,		/* which crosstalk device */
		device_desc_t dev_desc,		/* device descriptor */
		devfs_handle_t owner_dev)		/* owner of this interrupt, if known */
{
	return(do_hub_intr_alloc(dev, dev_desc, owner_dev, 0));
}

/*
 * Allocate resources required for an interrupt as specified in dev_desc.
 * Uncondtionally request non-threaded, regardless of what the device
 * descriptor might say.
 * Returns a hub interrupt handle on success, or 0 on failure.
 */
hub_intr_t
hub_intr_alloc_nothd(devfs_handle_t dev,		/* which crosstalk device */
		device_desc_t dev_desc,		/* device descriptor */
		devfs_handle_t owner_dev)		/* owner of this interrupt, if known */
{
	return(do_hub_intr_alloc(dev, dev_desc, owner_dev, 1));
}

/*
 * Free resources consumed by intr_alloc.
 */
void
hub_intr_free(hub_intr_t intr_hdl)
{
	cpuid_t cpu = intr_hdl->i_cpuid;
	int bit = intr_hdl->i_bit;
	xtalk_intr_t xtalk_info;

	if (intr_hdl->i_flags & HUB_INTR_IS_CONNECTED) {
		/* Setting the following fields in the xtalk interrupt info
	 	 * clears the interrupt target register in the xtalk user
	 	 */
		xtalk_info = &intr_hdl->i_xtalk_info;
		xtalk_info->xi_dev = NODEV;
		xtalk_info->xi_vector = 0;
		xtalk_info->xi_addr = 0;
		hub_intr_disconnect(intr_hdl);
	}

	if (intr_hdl->i_flags & HUB_INTR_IS_ALLOCED)
		kfree(intr_hdl);

	intr_unreserve_level(cpu, bit);
}


/*
 * Associate resources allocated with a previous hub_intr_alloc call with the
 * described handler, arg, name, etc.
 */
/*ARGSUSED*/
int
hub_intr_connect(	hub_intr_t intr_hdl,		/* xtalk intr resource handle */
			xtalk_intr_setfunc_t setfunc,	/* func to set intr hw */
			void *setfunc_arg)		/* arg to setfunc */
{
	int rv;
	cpuid_t cpu = intr_hdl->i_cpuid;
	int bit = intr_hdl->i_bit;
	extern int synergy_intr_connect(int, int);

	ASSERT(intr_hdl->i_flags & HUB_INTR_IS_ALLOCED);

	rv = intr_connect_level(cpu, bit, intr_hdl->i_swlevel, NULL);
	if (rv < 0)
		return(rv);

	intr_hdl->i_xtalk_info.xi_setfunc = setfunc;
	intr_hdl->i_xtalk_info.xi_sfarg = setfunc_arg;

	if (setfunc) (*setfunc)((xtalk_intr_t)intr_hdl);

	intr_hdl->i_flags |= HUB_INTR_IS_CONNECTED;
	return(synergy_intr_connect((int)bit, (int)cpu));
}


/*
 * Disassociate handler with the specified interrupt.
 */
void
hub_intr_disconnect(hub_intr_t intr_hdl)
{
	/*REFERENCED*/
	int rv;
	cpuid_t cpu = intr_hdl->i_cpuid;
	int bit = intr_hdl->i_bit;
	xtalk_intr_setfunc_t setfunc;

	setfunc = intr_hdl->i_xtalk_info.xi_setfunc;

	/* TBD: send disconnected interrupts somewhere harmless */
	if (setfunc) (*setfunc)((xtalk_intr_t)intr_hdl);

	rv = intr_disconnect_level(cpu, bit);
	ASSERT(rv == 0);
	intr_hdl->i_flags &= ~HUB_INTR_IS_CONNECTED;
}


/*
 * Return a hwgraph vertex that represents the CPU currently
 * targeted by an interrupt.
 */
devfs_handle_t
hub_intr_cpu_get(hub_intr_t intr_hdl)
{
	cpuid_t cpuid = intr_hdl->i_cpuid;
	ASSERT(cpuid != CPU_NONE);

	return(cpuid_to_vertex(cpuid));
}
