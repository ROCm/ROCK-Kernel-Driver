/*
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * Copyright (C) 2000 - 2001 by Kanoj Sarcar (kanoj@sgi.com)
 * Copyright (C) 2000 - 2001 by Silicon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mmzone.h>	/* for numnodes */
#include <linux/mm.h>
#include <linux/cpumask.h>
#include <asm/cpu.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/sn/types.h>
#include <asm/sn/sn0/addrs.h>
#include <asm/sn/sn0/hubni.h>
#include <asm/sn/sn0/hubio.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/ioc3.h>
#include <asm/mipsregs.h>
#include <asm/sn/gda.h>
#include <asm/sn/intr.h>
#include <asm/current.h>
#include <asm/smp.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/thread_info.h>
#include <asm/sn/launch.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/sn/mapped_kernel.h>

#define CPU_NONE		(cpuid_t)-1

static DECLARE_BITMAP(hub_init_mask, MAX_COMPACT_NODES);
static hubreg_t region_mask;
static int	fine_mode;
static int router_distance;
nasid_t master_nasid = INVALID_NASID;

cnodeid_t	nasid_to_compact_node[MAX_NASIDS];
nasid_t		compact_to_nasid_node[MAX_COMPACT_NODES];
cnodeid_t	cpuid_to_compact_node[MAXCPUS];
char		node_distances[MAX_COMPACT_NODES][MAX_COMPACT_NODES];

static hubreg_t get_region(cnodeid_t cnode)
{
	if (fine_mode)
		return COMPACT_TO_NASID_NODEID(cnode) >> NASID_TO_FINEREG_SHFT;
	else
		return COMPACT_TO_NASID_NODEID(cnode) >> NASID_TO_COARSEREG_SHFT;
}

static void gen_region_mask(hubreg_t *region_mask, int maxnodes)
{
	cnodeid_t cnode;

	(*region_mask) = 0;
	for (cnode = 0; cnode < maxnodes; cnode++) {
		(*region_mask) |= 1ULL << get_region(cnode);
	}
}

static int is_fine_dirmode(void)
{
	return (((LOCAL_HUB_L(NI_STATUS_REV_ID) & NSRI_REGIONSIZE_MASK)
		>> NSRI_REGIONSIZE_SHFT) & REGIONSIZE_FINE);
}

extern void pcibr_setup(cnodeid_t);
void per_hub_init(cnodeid_t cnode)
{
	nasid_t		nasid = COMPACT_TO_NASID_NODEID(cnode);

	if (test_and_set_bit(cnode, hub_init_mask))
		return;

	/*
	 * Set CRB timeout at 5ms, (< PI timeout of 10ms)
	 */
	REMOTE_HUB_S(nasid, IIO_ICTP, 0x800);
	REMOTE_HUB_S(nasid, IIO_ICTO, 0xff);

	hub_rtc_init(cnode);
	pcibr_setup(cnode);

#ifdef CONFIG_REPLICATE_EXHANDLERS
	/*
	 * If this is not a headless node initialization,
	 * copy over the caliased exception handlers.
	 */
	if (get_compact_nodeid() == cnode) {
		extern char except_vec0, except_vec1_r10k;
		extern char except_vec2_generic, except_vec3_generic;

		memcpy((void *)(KSEG0 + 0x100), &except_vec2_generic, 0x80);
		memcpy((void *)(KSEG0 + 0x180), &except_vec3_generic, 0x80);
		memcpy((void *)KSEG0, &except_vec0, 0x80);
		memcpy((void *)KSEG0 + 0x080, &except_vec1_r10k, 0x80);
		memcpy((void *)(KSEG0 + 0x100), (void *) KSEG0, 0x80);
		memcpy((void *)(KSEG0 + 0x180), &except_vec3_generic, 0x100);
		__flush_cache_all();
	}
#endif
}

cnodeid_t get_compact_nodeid(void)
{
	nasid_t nasid;

	nasid = get_nasid();
	/*
	 * Map the physical node id to a virtual node id (virtual node ids
	 * are contiguous).
	 */
	return NASID_TO_COMPACT_NODEID(nasid);
}

#define	rou_rflag	rou_flags

static void router_recurse(klrou_t *router_a, klrou_t *router_b, int depth)
{
	klrou_t *router;
	lboard_t *brd;
	int	port;

	if (router_a->rou_rflag == 1)
		return;

	if (depth >= router_distance)
		return;

	router_a->rou_rflag = 1;

	for (port = 1; port <= MAX_ROUTER_PORTS; port++) {
		if (router_a->rou_port[port].port_nasid == INVALID_NASID)
			continue;

		brd = (lboard_t *)NODE_OFFSET_TO_K0(
			router_a->rou_port[port].port_nasid,
			router_a->rou_port[port].port_offset);

		if (brd->brd_type == KLTYPE_ROUTER) {
			router = (klrou_t *)NODE_OFFSET_TO_K0(NASID_GET(brd), brd->brd_compts[0]);
			if (router == router_b) {
				if (depth < router_distance)
					router_distance = depth;
			}
			else
				router_recurse(router, router_b, depth + 1);
		}
	}

	router_a->rou_rflag = 0;
}

static int node_distance(nasid_t nasid_a, nasid_t nasid_b)
{
	klrou_t *router, *router_a = NULL, *router_b = NULL;
	lboard_t *brd, *dest_brd;
	cnodeid_t cnode;
	nasid_t nasid;
	int port;

	/* Figure out which routers nodes in question are connected to */
	for (cnode = 0; cnode < numnodes; cnode++) {
		nasid = COMPACT_TO_NASID_NODEID(cnode);

		if (nasid == -1) continue;

		brd = find_lboard_class((lboard_t *)KL_CONFIG_INFO(nasid),
					KLTYPE_ROUTER);

		if (!brd)
			continue;

		do {
			if (brd->brd_flags & DUPLICATE_BOARD)
				continue;

			router = (klrou_t *)NODE_OFFSET_TO_K0(NASID_GET(brd), brd->brd_compts[0]);
			router->rou_rflag = 0;

			for (port = 1; port <= MAX_ROUTER_PORTS; port++) {
				if (router->rou_port[port].port_nasid == INVALID_NASID)
					continue;

				dest_brd = (lboard_t *)NODE_OFFSET_TO_K0(
					router->rou_port[port].port_nasid,
					router->rou_port[port].port_offset);

				if (dest_brd->brd_type == KLTYPE_IP27) {
					if (dest_brd->brd_nasid == nasid_a)
						router_a = router;
					if (dest_brd->brd_nasid == nasid_b)
						router_b = router;
				}
			}

		} while ((brd = find_lboard_class(KLCF_NEXT(brd), KLTYPE_ROUTER)));
	}

	if (router_a == NULL) {
		printk("node_distance: router_a NULL\n");
		return -1;
	}
	if (router_b == NULL) {
		printk("node_distance: router_b NULL\n");
		return -1;
	}

	if (nasid_a == nasid_b)
		return 0;

	if (router_a == router_b)
		return 1;

	router_distance = 100;
	router_recurse(router_a, router_b, 2);

	return router_distance;
}

static void init_topology_matrix(void)
{
	nasid_t nasid, nasid2;
	cnodeid_t row, col;

	for (row = 0; row < MAX_COMPACT_NODES; row++)
		for (col = 0; col < MAX_COMPACT_NODES; col++)
			node_distances[row][col] = -1;

	for (row = 0; row < numnodes; row++) {
		nasid = COMPACT_TO_NASID_NODEID(row);
		for (col = 0; col < numnodes; col++) {
			nasid2 = COMPACT_TO_NASID_NODEID(col);
			node_distances[row][col] = node_distance(nasid, nasid2);
		}
	}
}

static void dump_topology(void)
{
	nasid_t nasid;
	cnodeid_t cnode;
	lboard_t *brd, *dest_brd;
	int port;
	int router_num = 0;
	klrou_t *router;
	cnodeid_t row, col;

	printk("************** Topology ********************\n");

	printk("    ");
	for (col = 0; col < numnodes; col++)
		printk("%02d ", col);
	printk("\n");
	for (row = 0; row < numnodes; row++) {
		printk("%02d  ", row);
		for (col = 0; col < numnodes; col++)
			printk("%2d ", node_distances[row][col]);
		printk("\n");
	}

	for (cnode = 0; cnode < numnodes; cnode++) {
		nasid = COMPACT_TO_NASID_NODEID(cnode);

		if (nasid == -1) continue;

		brd = find_lboard_class((lboard_t *)KL_CONFIG_INFO(nasid),
					KLTYPE_ROUTER);

		if (!brd)
			continue;

		do {
			if (brd->brd_flags & DUPLICATE_BOARD)
				continue;
			printk("Router %d:", router_num);
			router_num++;

			router = (klrou_t *)NODE_OFFSET_TO_K0(NASID_GET(brd), brd->brd_compts[0]);

			for (port = 1; port <= MAX_ROUTER_PORTS; port++) {
				if (router->rou_port[port].port_nasid == INVALID_NASID)
					continue;

				dest_brd = (lboard_t *)NODE_OFFSET_TO_K0(
					router->rou_port[port].port_nasid,
					router->rou_port[port].port_offset);

				if (dest_brd->brd_type == KLTYPE_IP27)
					printk(" %d", dest_brd->brd_nasid);
				if (dest_brd->brd_type == KLTYPE_ROUTER)
					printk(" r");
			}
			printk("\n");

		} while ( (brd = find_lboard_class(KLCF_NEXT(brd), KLTYPE_ROUTER)) );
	}
}

void mlreset(void)
{
	int i;

	master_nasid = get_nasid();
	fine_mode = is_fine_dirmode();

	/*
	 * Probe for all CPUs - this creates the cpumask and sets up the
	 * mapping tables.  We need to do this as early as possible.
	 */
#ifdef CONFIG_SMP
	cpu_node_probe();
#endif

	init_topology_matrix();
	dump_topology();

	gen_region_mask(&region_mask, numnodes);

	setup_replication_mask(numnodes);

	/*
	 * Set all nodes' calias sizes to 8k
	 */
	for (i = 0; i < numnodes; i++) {
		nasid_t nasid;

		nasid = COMPACT_TO_NASID_NODEID(i);

		/*
		 * Always have node 0 in the region mask, otherwise
		 * CALIAS accesses get exceptions since the hub
		 * thinks it is a node 0 address.
		 */
		REMOTE_HUB_S(nasid, PI_REGION_PRESENT, (region_mask | 1));
#ifdef CONFIG_REPLICATE_EXHANDLERS
		REMOTE_HUB_S(nasid, PI_CALIAS_SIZE, PI_CALIAS_SIZE_8K);
#else
		REMOTE_HUB_S(nasid, PI_CALIAS_SIZE, PI_CALIAS_SIZE_0);
#endif

#ifdef LATER
		/*
		 * Set up all hubs to have a big window pointing at
		 * widget 0. Memory mode, widget 0, offset 0
		 */
		REMOTE_HUB_S(nasid, IIO_ITTE(SWIN0_BIGWIN),
			((HUB_PIO_MAP_TO_MEM << IIO_ITTE_IOSP_SHIFT) |
			(0 << IIO_ITTE_WIDGET_SHIFT)));
#endif
	}
}
