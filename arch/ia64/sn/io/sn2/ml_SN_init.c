/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/bootmem.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn_cpuid.h>

extern cpuid_t master_procid;
int		maxcpus;

extern xwidgetnum_t hub_widget_id(nasid_t);

extern void iograph_early_init(void);

nasid_t master_nasid = INVALID_NASID;		/* This is the partition master nasid */
nasid_t master_baseio_nasid = INVALID_NASID;	/* This is the master base I/O nasid */


/*
 * mlreset(void)
 * 	very early machine reset - at this point NO interrupts have been
 * 	enabled; nor is memory, tlb, p0, etc setup.
 *
 * 	slave is zero when mlreset is called for the master processor and
 *	is nonzero thereafter.
 */


void
mlreset(int slave)
{
	/*
	 * We are the master cpu and node.
	 */ 
	master_nasid = get_nasid();
	set_master_bridge_base();

	/* We're the master processor */
	master_procid = smp_processor_id();
	master_nasid = cpuid_to_nasid(master_procid);

	/*
	 * master_nasid we get back better be same as one from
	 * get_nasid()
	 */
	ASSERT_ALWAYS(master_nasid == get_nasid());

	/* early initialization of iograph */
	iograph_early_init();
}


/* XXX - Move the meat of this to intr.c ? */
/*
 * Set up the platform-dependent fields in the nodepda.
 */
void init_platform_nodepda(nodepda_t *npda, cnodeid_t node)
{
	hubinfo_t hubinfo;

	extern void router_map_init(nodepda_t *);
	extern void router_queue_init(nodepda_t *,cnodeid_t);
	extern void intr_init_vecblk(nodepda_t *, cnodeid_t, int);

	/* Allocate per-node platform-dependent data */
	hubinfo = (hubinfo_t)alloc_bootmem_node(NODE_DATA(node), sizeof(struct hubinfo_s));

	npda->pdinfo = (void *)hubinfo;
	hubinfo->h_nodepda = npda;
	hubinfo->h_cnodeid = node;
	hubinfo->h_nasid = COMPACT_TO_NASID_NODEID(node);

	spin_lock_init(&hubinfo->h_crblock);

	hubinfo->h_widgetid = hub_widget_id(hubinfo->h_nasid);
	npda->xbow_peer = INVALID_NASID;

	/* 
	 * Initialize the linked list of
	 * router info pointers to the dependent routers
	 */
	npda->npda_rip_first = NULL;

	/*
	 * npda_rip_last always points to the place
	 * where the next element is to be inserted
	 * into the list 
	 */
	npda->npda_rip_last = &npda->npda_rip_first;
	npda->geoid.any.type = GEO_TYPE_INVALID;

	mutex_init_locked(&npda->xbow_sema); /* init it locked? */
}

void
update_node_information(cnodeid_t cnodeid)
{
	nodepda_t *npda = NODEPDA(cnodeid);
	nodepda_router_info_t *npda_rip;
	
	/* Go through the list of router info 
	 * structures and copy some frequently
	 * accessed info from the info hanging
	 * off the corresponding router vertices
	 */
	npda_rip = npda->npda_rip_first;
	while(npda_rip) {
		if (npda_rip->router_infop) {
			npda_rip->router_portmask = 
				npda_rip->router_infop->ri_portmask;
			npda_rip->router_slot = 
				npda_rip->router_infop->ri_slotnum;
		} else {
			/* No router, no ports. */
			npda_rip->router_portmask = 0;
		}
		npda_rip = npda_rip->router_next;
	}
}
