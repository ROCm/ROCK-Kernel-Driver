/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2002 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <asm/sn/sgi.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/xtalk/xwidget.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>
#include <asm/sn/prio.h>
#include <asm/sn/xtalk/xbow.h>
#include <asm/sn/ioc3.h>
#include <asm/sn/eeprom.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_private.h>

void              do_pcibr_rrb_clear(bridge_t *, int);
void              do_pcibr_rrb_flush(bridge_t *, int);
int               do_pcibr_rrb_count_valid(bridge_t *, pciio_slot_t);
int               do_pcibr_rrb_count_avail(bridge_t *, pciio_slot_t);
int               do_pcibr_rrb_alloc(bridge_t *, pciio_slot_t, int);
int               do_pcibr_rrb_free(bridge_t *, pciio_slot_t, int);

void              do_pcibr_rrb_autoalloc(pcibr_soft_t, int, int);

int		  pcibr_wrb_flush(devfs_handle_t);
int               pcibr_rrb_alloc(devfs_handle_t, int *, int *);
int               pcibr_rrb_check(devfs_handle_t, int *, int *, int *, int *);
int               pcibr_alloc_all_rrbs(devfs_handle_t, int, int, int, int, int, int, int, int, int);
void              pcibr_rrb_flush(devfs_handle_t);
int		  pcibr_slot_initial_rrb_alloc(devfs_handle_t,pciio_slot_t);

/* 
 *    RRB Management
 */

#define LSBIT(word)		((word) &~ ((word)-1))

void
do_pcibr_rrb_clear(bridge_t *bridge, int rrb)
{
    bridgereg_t             status;

    /* bridge_lock must be held;
     * this RRB must be disabled.
     */

    /* wait until RRB has no outstanduing XIO packets. */
    while ((status = bridge->b_resp_status) & BRIDGE_RRB_INUSE(rrb)) {
	;				/* XXX- beats on bridge. bad idea? */
    }

    /* if the RRB has data, drain it. */
    if (status & BRIDGE_RRB_VALID(rrb)) {
	bridge->b_resp_clear = BRIDGE_RRB_CLEAR(rrb);

	/* wait until RRB is no longer valid. */
	while ((status = bridge->b_resp_status) & BRIDGE_RRB_VALID(rrb)) {
	    ;				/* XXX- beats on bridge. bad idea? */
	}
    }
}

void
do_pcibr_rrb_flush(bridge_t *bridge, int rrbn)
{
    reg_p                   rrbp = &bridge->b_rrb_map[rrbn & 1].reg;
    bridgereg_t             rrbv;
    int                     shft = 4 * (rrbn >> 1);
    unsigned                ebit = BRIDGE_RRB_EN << shft;

    rrbv = *rrbp;
    if (rrbv & ebit)
	*rrbp = rrbv & ~ebit;

    do_pcibr_rrb_clear(bridge, rrbn);

    if (rrbv & ebit)
	*rrbp = rrbv;
}

/*
 *    pcibr_rrb_count_valid: count how many RRBs are
 *      marked valid for the specified PCI slot on this
 *      bridge.
 *
 *      NOTE: The "slot" parameter for all pcibr_rrb
 *      management routines must include the "virtual"
 *      bit; when manageing both the normal and the
 *      virtual channel, separate calls to these
 *      routines must be made. To denote the virtual
 *      channel, add PCIBR_RRB_SLOT_VIRTUAL to the slot
 *      number.
 *
 *      IMPL NOTE: The obvious algorithm is to iterate
 *      through the RRB fields, incrementing a count if
 *      the RRB is valid and matches the slot. However,
 *      it is much simpler to use an algorithm derived
 *      from the "partitioned add" idea. First, XOR in a
 *      pattern such that the fields that match this
 *      slot come up "all ones" and all other fields
 *      have zeros in the mismatching bits. Then AND
 *      together the bits in the field, so we end up
 *      with one bit turned on for each field that
 *      matched. Now we need to count these bits. This
 *      can be done either with a series of shift/add
 *      instructions or by using "tmp % 15"; I expect
 *      that the cascaded shift/add will be faster.
 */

int
do_pcibr_rrb_count_valid(bridge_t *bridge,
			 pciio_slot_t slot)
{
    bridgereg_t             tmp;

    tmp = bridge->b_rrb_map[slot & 1].reg;
    tmp ^= 0x11111111 * (7 - slot / 2);
    tmp &= (0xCCCCCCCC & tmp) >> 2;
    tmp &= (0x22222222 & tmp) >> 1;
    tmp += tmp >> 4;
    tmp += tmp >> 8;
    tmp += tmp >> 16;
    return tmp & 15;
}

/*
 *    do_pcibr_rrb_count_avail: count how many RRBs are
 *      available to be allocated for the specified slot.
 *
 *      IMPL NOTE: similar to the above, except we are
 *      just counting how many fields have the valid bit
 *      turned off.
 */
int
do_pcibr_rrb_count_avail(bridge_t *bridge,
			 pciio_slot_t slot)
{
    bridgereg_t             tmp;

    tmp = bridge->b_rrb_map[slot & 1].reg;
    tmp = (0x88888888 & ~tmp) >> 3;
    tmp += tmp >> 4;
    tmp += tmp >> 8;
    tmp += tmp >> 16;
    return tmp & 15;
}

/*
 *    do_pcibr_rrb_alloc: allocate some additional RRBs
 *      for the specified slot. Returns -1 if there were
 *      insufficient free RRBs to satisfy the request,
 *      or 0 if the request was fulfilled.
 *
 *      Note that if a request can be partially filled,
 *      it will be, even if we return failure.
 *
 *      IMPL NOTE: again we avoid iterating across all
 *      the RRBs; instead, we form up a word containing
 *      one bit for each free RRB, then peel the bits
 *      off from the low end.
 */
int
do_pcibr_rrb_alloc(bridge_t *bridge,
		   pciio_slot_t slot,
		   int more)
{
    int                     rv = 0;
    bridgereg_t             reg, tmp, bit;

    reg = bridge->b_rrb_map[slot & 1].reg;
    tmp = (0x88888888 & ~reg) >> 3;
    while (more-- > 0) {
	bit = LSBIT(tmp);
	if (!bit) {
	    rv = -1;
	    break;
	}
	tmp &= ~bit;
	reg = ((reg & ~(bit * 15)) | (bit * (8 + slot / 2)));
    }
    bridge->b_rrb_map[slot & 1].reg = reg;
    return rv;
}

/*
 *    do_pcibr_rrb_free: release some of the RRBs that
 *      have been allocated for the specified
 *      slot. Returns zero for success, or negative if
 *      it was unable to free that many RRBs.
 *
 *      IMPL NOTE: We form up a bit for each RRB
 *      allocated to the slot, aligned with the VALID
 *      bitfield this time; then we peel bits off one at
 *      a time, releasing the corresponding RRB.
 */
int
do_pcibr_rrb_free(bridge_t *bridge,
		  pciio_slot_t slot,
		  int less)
{
    int                     rv = 0;
    bridgereg_t             reg, tmp, clr, bit;
    int                     i;

    clr = 0;
    reg = bridge->b_rrb_map[slot & 1].reg;

    /* This needs to be done otherwise the rrb's on the virtual channel
     * for this slot won't be freed !!
     */
    tmp = reg & 0xbbbbbbbb;

    tmp ^= (0x11111111 * (7 - slot / 2));
    tmp &= (0x33333333 & tmp) << 2;
    tmp &= (0x44444444 & tmp) << 1;
    while (less-- > 0) {
	bit = LSBIT(tmp);
	if (!bit) {
	    rv = -1;
	    break;
	}
	tmp &= ~bit;
	reg &= ~bit;
	clr |= bit;
    }
    bridge->b_rrb_map[slot & 1].reg = reg;

    for (i = 0; i < 8; i++)
	if (clr & (8 << (4 * i)))
	    do_pcibr_rrb_clear(bridge, (2 * i) + (slot & 1));

    return rv;
}

void
do_pcibr_rrb_autoalloc(pcibr_soft_t pcibr_soft,
		       int slot,
		       int more_rrbs)
{
    bridge_t               *bridge = pcibr_soft->bs_base;
    int                     got;

    for (got = 0; got < more_rrbs; ++got) {
	if (pcibr_soft->bs_rrb_res[slot & 7] > 0)
	    pcibr_soft->bs_rrb_res[slot & 7]--;
	else if (pcibr_soft->bs_rrb_avail[slot & 1] > 0)
	    pcibr_soft->bs_rrb_avail[slot & 1]--;
	else
	    break;
	if (do_pcibr_rrb_alloc(bridge, slot, 1) < 0)
	    break;
#if PCIBR_RRB_DEBUG
	printk("do_pcibr_rrb_autoalloc: add one to slot %d%s\n",
		slot & 7, slot & 8 ? "v" : "");
#endif
	pcibr_soft->bs_rrb_valid[slot]++;
    }
#if PCIBR_RRB_DEBUG
    printk("%s: %d+%d free RRBs. Allocation list:\n", pcibr_soft->bs_name,
	    pcibr_soft->bs_rrb_avail[0],
	    pcibr_soft->bs_rrb_avail[1]);
    for (slot = 0; slot < 8; ++slot)
	printk("\t%d+%d+%d",
		0xFFF & pcibr_soft->bs_rrb_valid[slot],
		0xFFF & pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL],
		pcibr_soft->bs_rrb_res[slot]);
    printk("\n");
#endif
}

/*
 * Device driver interface to flush the write buffers for a specified
 * device hanging off the bridge.
 */
int
pcibr_wrb_flush(devfs_handle_t pconn_vhdl)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    bridge_t               *bridge = pcibr_soft->bs_base;
    volatile bridgereg_t   *wrb_flush;

    wrb_flush = &(bridge->b_wr_req_buf[pciio_slot].reg);
    while (*wrb_flush);

    return(0);
}

/*
 * Device driver interface to request RRBs for a specified device
 * hanging off a Bridge.  The driver requests the total number of
 * RRBs it would like for the normal channel (vchan0) and for the
 * "virtual channel" (vchan1).  The actual number allocated to each
 * channel is returned.
 *
 * If we cannot allocate at least one RRB to a channel that needs
 * at least one, return -1 (failure).  Otherwise, satisfy the request
 * as best we can and return 0.
 */
int
pcibr_rrb_alloc(devfs_handle_t pconn_vhdl,
		int *count_vchan0,
		int *count_vchan1)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    bridge_t               *bridge = pcibr_soft->bs_base;
    int                     desired_vchan0;
    int                     desired_vchan1;
    int                     orig_vchan0;
    int                     orig_vchan1;
    int                     delta_vchan0;
    int                     delta_vchan1;
    int                     final_vchan0;
    int                     final_vchan1;
    int                     avail_rrbs;
    int                     res_rrbs;
    unsigned long           s;
    int                     error;

    /*
     * TBD: temper request with admin info about RRB allocation,
     * and according to demand from other devices on this Bridge.
     *
     * One way of doing this would be to allocate two RRBs
     * for each device on the bus, before any drivers start
     * asking for extras. This has the weakness that one
     * driver might not give back an "extra" RRB until after
     * another driver has already failed to get one that
     * it wanted.
     */

    s = pcibr_lock(pcibr_soft);

    /* Save the boot-time RRB configuration for this slot */
    if (pcibr_soft->bs_rrb_valid_dflt[pciio_slot] < 0) {
        pcibr_soft->bs_rrb_valid_dflt[pciio_slot] =
                pcibr_soft->bs_rrb_valid[pciio_slot]; 
        pcibr_soft->bs_rrb_valid_dflt[pciio_slot + PCIBR_RRB_SLOT_VIRTUAL] =
                pcibr_soft->bs_rrb_valid[pciio_slot + PCIBR_RRB_SLOT_VIRTUAL];
        pcibr_soft->bs_rrb_res_dflt[pciio_slot] =
                pcibr_soft->bs_rrb_res[pciio_slot];
                  
    }

    /* How many RRBs do we own? */
    orig_vchan0 = pcibr_soft->bs_rrb_valid[pciio_slot];
    orig_vchan1 = pcibr_soft->bs_rrb_valid[pciio_slot + PCIBR_RRB_SLOT_VIRTUAL];

    /* How many RRBs do we want? */
    desired_vchan0 = count_vchan0 ? *count_vchan0 : orig_vchan0;
    desired_vchan1 = count_vchan1 ? *count_vchan1 : orig_vchan1;

    /* How many RRBs are free? */
    avail_rrbs = pcibr_soft->bs_rrb_avail[pciio_slot & 1]
	+ pcibr_soft->bs_rrb_res[pciio_slot];

    /* Figure desired deltas */
    delta_vchan0 = desired_vchan0 - orig_vchan0;
    delta_vchan1 = desired_vchan1 - orig_vchan1;

    /* Trim back deltas to something
     * that we can actually meet, by
     * decreasing the ending allocation
     * for whichever channel wants
     * more RRBs. If both want the same
     * number, cut the second channel.
     * NOTE: do not change the allocation for
     * a channel that was passed as NULL.
     */
    while ((delta_vchan0 + delta_vchan1) > avail_rrbs) {
	if (count_vchan0 &&
	    (!count_vchan1 ||
	     ((orig_vchan0 + delta_vchan0) >
	      (orig_vchan1 + delta_vchan1))))
	    delta_vchan0--;
	else
	    delta_vchan1--;
    }

    /* Figure final RRB allocations
     */
    final_vchan0 = orig_vchan0 + delta_vchan0;
    final_vchan1 = orig_vchan1 + delta_vchan1;

    /* If either channel wants RRBs but our actions
     * would leave it with none, declare an error,
     * but DO NOT change any RRB allocations.
     */
    if ((desired_vchan0 && !final_vchan0) ||
	(desired_vchan1 && !final_vchan1)) {

	error = -1;

    } else {

	/* Commit the allocations: free, then alloc.
	 */
	if (delta_vchan0 < 0)
	    (void) do_pcibr_rrb_free(bridge, pciio_slot, -delta_vchan0);
	if (delta_vchan1 < 0)
	    (void) do_pcibr_rrb_free(bridge, PCIBR_RRB_SLOT_VIRTUAL + pciio_slot, -delta_vchan1);

	if (delta_vchan0 > 0)
	    (void) do_pcibr_rrb_alloc(bridge, pciio_slot, delta_vchan0);
	if (delta_vchan1 > 0)
	    (void) do_pcibr_rrb_alloc(bridge, PCIBR_RRB_SLOT_VIRTUAL + pciio_slot, delta_vchan1);

	/* Return final values to caller.
	 */
	if (count_vchan0)
	    *count_vchan0 = final_vchan0;
	if (count_vchan1)
	    *count_vchan1 = final_vchan1;

	/* prevent automatic changes to this slot's RRBs
	 */
	pcibr_soft->bs_rrb_fixed |= 1 << pciio_slot;

	/* Track the actual allocations, release
	 * any further reservations, and update the
	 * number of available RRBs.
	 */

	pcibr_soft->bs_rrb_valid[pciio_slot] = final_vchan0;
	pcibr_soft->bs_rrb_valid[pciio_slot + PCIBR_RRB_SLOT_VIRTUAL] = final_vchan1;
	pcibr_soft->bs_rrb_avail[pciio_slot & 1] =
	    pcibr_soft->bs_rrb_avail[pciio_slot & 1]
	    + pcibr_soft->bs_rrb_res[pciio_slot]
	    - delta_vchan0
	    - delta_vchan1;
	pcibr_soft->bs_rrb_res[pciio_slot] = 0;

        /*
         * Reserve enough RRBs so this slot's RRB configuration can be
         * reset to its boot-time default following a hot-plug shut-down
         */
        res_rrbs =   (pcibr_soft->bs_rrb_valid_dflt[pciio_slot] -
                      pcibr_soft->bs_rrb_valid[pciio_slot]) 
                   + (pcibr_soft->bs_rrb_valid_dflt[pciio_slot +
                                                    PCIBR_RRB_SLOT_VIRTUAL] -
                      pcibr_soft->bs_rrb_valid[pciio_slot +
                                               PCIBR_RRB_SLOT_VIRTUAL])
                   + (pcibr_soft->bs_rrb_res_dflt[pciio_slot] -
                      pcibr_soft->bs_rrb_res[pciio_slot]);

         if (res_rrbs > 0) {
             pcibr_soft->bs_rrb_res[pciio_slot] = res_rrbs;
             pcibr_soft->bs_rrb_avail[pciio_slot & 1] =
                 pcibr_soft->bs_rrb_avail[pciio_slot & 1]
                 - res_rrbs;
         }
 
#if PCIBR_RRB_DEBUG
	printk("pcibr_rrb_alloc: slot %d set to %d+%d; %d+%d free\n",
		pciio_slot, final_vchan0, final_vchan1,
		pcibr_soft->bs_rrb_avail[0],
		pcibr_soft->bs_rrb_avail[1]);
	for (pciio_slot = 0; pciio_slot < 8; ++pciio_slot)
	    printk("\t%d+%d+%d",
		    0xFFF & pcibr_soft->bs_rrb_valid[pciio_slot],
		    0xFFF & pcibr_soft->bs_rrb_valid[pciio_slot + PCIBR_RRB_SLOT_VIRTUAL],
		    pcibr_soft->bs_rrb_res[pciio_slot]);
	printk("\n");
#endif

	error = 0;
    }

    pcibr_unlock(pcibr_soft, s);

    return error;
}

/*
 * Device driver interface to check the current state
 * of the RRB allocations.
 *
 *   pconn_vhdl is your PCI connection point (specifies which
 *      PCI bus and which slot).
 *
 *   count_vchan0 points to where to return the number of RRBs
 *      assigned to the primary DMA channel, used by all DMA
 *      that does not explicitly ask for the alternate virtual
 *      channel.
 *
 *   count_vchan1 points to where to return the number of RRBs
 *      assigned to the secondary DMA channel, used when
 *      PCIBR_VCHAN1 and PCIIO_DMA_A64 are specified.
 *
 *   count_reserved points to where to return the number of RRBs
 *      that have been automatically reserved for your device at
 *      startup, but which have not been assigned to a
 *      channel. RRBs must be assigned to a channel to be used;
 *      this can be done either with an explicit pcibr_rrb_alloc
 *      call, or automatically by the infrastructure when a DMA
 *      translation is constructed. Any call to pcibr_rrb_alloc
 *      will release any unassigned reserved RRBs back to the
 *      free pool.
 *
 *   count_pool points to where to return the number of RRBs
 *      that are currently unassigned and unreserved. This
 *      number can (and will) change as other drivers make calls
 *      to pcibr_rrb_alloc, or automatically allocate RRBs for
 *      DMA beyond their initial reservation.
 *
 * NULL may be passed for any of the return value pointers
 * the caller is not interested in.
 *
 * The return value is "0" if all went well, or "-1" if
 * there is a problem. Additionally, if the wrong vertex
 * is passed in, one of the subsidiary support functions
 * could panic with a "bad pciio fingerprint."
 */

int
pcibr_rrb_check(devfs_handle_t pconn_vhdl,
		int *count_vchan0,
		int *count_vchan1,
		int *count_reserved,
		int *count_pool)
{
    pciio_info_t            pciio_info;
    pciio_slot_t            pciio_slot;
    pcibr_soft_t            pcibr_soft;
    unsigned long           s;
    int                     error = -1;

    if ((pciio_info = pciio_info_get(pconn_vhdl)) &&
	(pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info)) &&
	((pciio_slot = pciio_info_slot_get(pciio_info)) < 8)) {

	s = pcibr_lock(pcibr_soft);

	if (count_vchan0)
	    *count_vchan0 =
		pcibr_soft->bs_rrb_valid[pciio_slot];

	if (count_vchan1)
	    *count_vchan1 =
		pcibr_soft->bs_rrb_valid[pciio_slot + PCIBR_RRB_SLOT_VIRTUAL];

	if (count_reserved)
	    *count_reserved =
		pcibr_soft->bs_rrb_res[pciio_slot];

	if (count_pool)
	    *count_pool =
		pcibr_soft->bs_rrb_avail[pciio_slot & 1];

	error = 0;

	pcibr_unlock(pcibr_soft, s);
    }
    return error;
}

/* pcibr_alloc_all_rrbs allocates all the rrbs available in the quantities
 * requested for each of the devices.  The evn_odd argument indicates whether
 * allocation is for the odd or even rrbs. The next group of four argument
 * pairs indicate the amount of rrbs to be assigned to each device. The first
 * argument of each pair indicate the total number of rrbs to allocate for that
 * device. The second argument of each pair indicates how many rrb's from the
 * first argument should be assigned to the virtual channel. The total of all
 * of the first arguments should be <= 8. The second argument should be <= the
 * first argument.
 * if even_odd = 0 the devices in order are 0, 2, 4, 6 
 * if even_odd = 1 the devices in order are 1, 3, 5, 7
 * returns 0 if no errors else returns -1
 */

int
pcibr_alloc_all_rrbs(devfs_handle_t vhdl, int even_odd,
		     int dev_1_rrbs, int virt1, int dev_2_rrbs, int virt2,
		     int dev_3_rrbs, int virt3, int dev_4_rrbs, int virt4)
{
    devfs_handle_t          pcibr_vhdl;
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t)0;
    bridge_t               *bridge = NULL;

    uint32_t                rrb_setting = 0;
    int                     rrb_shift = 7;
    uint32_t                cur_rrb;
    int                     dev_rrbs[4];
    int                     virt[4];
    int                     i, j;
    unsigned long           s;

    if (GRAPH_SUCCESS ==
	hwgraph_traverse(vhdl, EDGE_LBL_PCI, &pcibr_vhdl)) {
	pcibr_soft = pcibr_soft_get(pcibr_vhdl);
	if (pcibr_soft)
	    bridge = pcibr_soft->bs_base;
	hwgraph_vertex_unref(pcibr_vhdl);
    }
    if (bridge == NULL)
	bridge = (bridge_t *) xtalk_piotrans_addr
	    (vhdl, NULL, 0, sizeof(bridge_t), 0);

    even_odd &= 1;

    dev_rrbs[0] = dev_1_rrbs;
    dev_rrbs[1] = dev_2_rrbs;
    dev_rrbs[2] = dev_3_rrbs;
    dev_rrbs[3] = dev_4_rrbs;

    virt[0] = virt1;
    virt[1] = virt2;
    virt[2] = virt3;
    virt[3] = virt4;

    if ((dev_1_rrbs + dev_2_rrbs + dev_3_rrbs + dev_4_rrbs) > 8) {
	return -1;
    }
    if ((dev_1_rrbs < 0) || (dev_2_rrbs < 0) || (dev_3_rrbs < 0) || (dev_4_rrbs < 0)) {
	return -1;
    }
    /* walk through rrbs */
    for (i = 0; i < 4; i++) {
	if (virt[i]) {
		for( j = 0; j < virt[i]; j++) {
			cur_rrb = i | 0xc;
			cur_rrb = cur_rrb << (rrb_shift * 4);
			rrb_shift--;
			rrb_setting = rrb_setting | cur_rrb;
			dev_rrbs[i] = dev_rrbs[i] - 1;
		}
	}
	for (j = 0; j < dev_rrbs[i]; j++) {
	    cur_rrb = i | 0x8;
	    cur_rrb = cur_rrb << (rrb_shift * 4);
	    rrb_shift--;
	    rrb_setting = rrb_setting | cur_rrb;
	}
    }

    if (pcibr_soft)
	s = pcibr_lock(pcibr_soft);

    bridge->b_rrb_map[even_odd].reg = rrb_setting;

    if (pcibr_soft) {

	pcibr_soft->bs_rrb_fixed |= 0x55 << even_odd;

	/* since we've "FIXED" the allocations
	 * for these slots, we probably can dispense
	 * with tracking avail/res/valid data, but
	 * keeping it up to date helps debugging.
	 */

	pcibr_soft->bs_rrb_avail[even_odd] =
	    8 - (dev_1_rrbs + dev_2_rrbs + dev_3_rrbs + dev_4_rrbs);

	pcibr_soft->bs_rrb_res[even_odd + 0] = 0;
	pcibr_soft->bs_rrb_res[even_odd + 2] = 0;
	pcibr_soft->bs_rrb_res[even_odd + 4] = 0;
	pcibr_soft->bs_rrb_res[even_odd + 6] = 0;

	pcibr_soft->bs_rrb_valid[even_odd + 0] = dev_1_rrbs - virt1;
	pcibr_soft->bs_rrb_valid[even_odd + 2] = dev_2_rrbs - virt2;
	pcibr_soft->bs_rrb_valid[even_odd + 4] = dev_3_rrbs - virt3;
	pcibr_soft->bs_rrb_valid[even_odd + 6] = dev_4_rrbs - virt4;

	pcibr_soft->bs_rrb_valid[even_odd + 0 + PCIBR_RRB_SLOT_VIRTUAL] = virt1;
	pcibr_soft->bs_rrb_valid[even_odd + 2 + PCIBR_RRB_SLOT_VIRTUAL] = virt2;
	pcibr_soft->bs_rrb_valid[even_odd + 4 + PCIBR_RRB_SLOT_VIRTUAL] = virt3;
	pcibr_soft->bs_rrb_valid[even_odd + 6 + PCIBR_RRB_SLOT_VIRTUAL] = virt4;

	pcibr_unlock(pcibr_soft, s);
    }
    return 0;
}

/*
 *    pcibr_rrb_flush: chase down all the RRBs assigned
 *      to the specified connection point, and flush
 *      them.
 */
void
pcibr_rrb_flush(devfs_handle_t pconn_vhdl)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    bridge_t               *bridge = pcibr_soft->bs_base;
    unsigned long           s;
    reg_p                   rrbp;
    unsigned                rrbm;
    int                     i;
    int                     rrbn;
    unsigned                sval;
    unsigned                mask;

    sval = BRIDGE_RRB_EN | (pciio_slot >> 1);
    mask = BRIDGE_RRB_EN | BRIDGE_RRB_PDEV;
    rrbn = pciio_slot & 1;
    rrbp = &bridge->b_rrb_map[rrbn].reg;

    s = pcibr_lock(pcibr_soft);
    rrbm = *rrbp;
    for (i = 0; i < 8; ++i) {
	if ((rrbm & mask) == sval)
	    do_pcibr_rrb_flush(bridge, rrbn);
	rrbm >>= 4;
	rrbn += 2;
    }
    pcibr_unlock(pcibr_soft, s);
}

/*
 * pcibr_slot_initial_rrb_alloc
 *	Allocate a default number of rrbs for this slot on 
 * 	the two channels.  This is dictated by the rrb allocation
 * 	strategy routine defined per platform.
 */

int
pcibr_slot_initial_rrb_alloc(devfs_handle_t pcibr_vhdl,
			     pciio_slot_t slot)
{
    pcibr_soft_t	 pcibr_soft;
    pcibr_info_h	 pcibr_infoh;
    pcibr_info_t	 pcibr_info;
    bridge_t		*bridge;
    int                  c0, c1, r;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    bridge = pcibr_soft->bs_base;

    /* How may RRBs are on this slot?
     */
    c0 = do_pcibr_rrb_count_valid(bridge, slot);
    c1 = do_pcibr_rrb_count_valid(bridge, slot + PCIBR_RRB_SLOT_VIRTUAL);

#if PCIBR_RRB_DEBUG
    printk(
	    "pcibr_slot_initial_rrb_alloc: slot %d started with %d+%d\n",
            slot, c0, c1);
#endif

    /* Do we really need any?
     */
    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;
    pcibr_info = pcibr_infoh[0];
    if ((pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE) &&
	!pcibr_soft->bs_slot[slot].has_host) {
	if (c0 > 0)
	    do_pcibr_rrb_free(bridge, slot, c0);
	if (c1 > 0)
	    do_pcibr_rrb_free(bridge, slot + PCIBR_RRB_SLOT_VIRTUAL, c1);
	pcibr_soft->bs_rrb_valid[slot] = 0x1000;
	pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL] = 0x1000;
	return(ENODEV);
    }

    pcibr_soft->bs_rrb_avail[slot & 1] -= c0 + c1;
    pcibr_soft->bs_rrb_valid[slot] = c0;
    pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL] = c1;

    pcibr_soft->bs_rrb_avail[0] = do_pcibr_rrb_count_avail(bridge, 0);
    pcibr_soft->bs_rrb_avail[1] = do_pcibr_rrb_count_avail(bridge, 1);

    r = 3 - (c0 + c1);

    if (r > 0) {
	pcibr_soft->bs_rrb_res[slot] = r;
	pcibr_soft->bs_rrb_avail[slot & 1] -= r;
    }

#if PCIBR_RRB_DEBUG
    printk("\t%d+%d+%d",
	    0xFFF & pcibr_soft->bs_rrb_valid[slot],
	    0xFFF & pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL],
	    pcibr_soft->bs_rrb_res[slot]);
    printk("\n");
#endif

    return(0);
}

/*
 * pcibr_initial_rrb
 *      Assign an equal total number of RRBs to all candidate slots, 
 *      where the total is the sum of the number of RRBs assigned to
 *      the normal channel, the number of RRBs assigned to the virtual
 *      channel, and the number of RRBs assigned as reserved. 
 *
 *      A candidate slot is a populated slot on a non-SN1 system or 
 *      any existing (populated or empty) slot on an SN1 system.
 *      Empty SN1 slots need RRBs to support hot-plug operations.
 */

int
pcibr_initial_rrb(devfs_handle_t pcibr_vhdl,
			     pciio_slot_t first, pciio_slot_t last)
{
    pcibr_soft_t            pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    bridge_t               *bridge = pcibr_soft->bs_base;
    pciio_slot_t            slot;
    int                     c0, c1;
    int                     have[2][3];
    int                     res[2];
    int                     eo;

    have[0][0] = have[0][1] = have[0][2] = 0;
    have[1][0] = have[1][1] = have[1][2] = 0;
    res[0] = res[1] = 0;

    for (slot = 0; slot < 8; ++slot) {
        /* Initial RRB management; give back RRBs in all non-existent slots */
        (void) pcibr_slot_initial_rrb_alloc(pcibr_vhdl, slot);

        /* Base calculations only on existing slots */
        if ((slot >= first) && (slot <= last)) {
            c0 = pcibr_soft->bs_rrb_valid[slot];
            c1 = pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL];
            if ((c0 + c1) < 3)
                have[slot & 1][c0 + c1]++;
        }
    }

    /* Initialize even/odd slot available RRB counts */
    pcibr_soft->bs_rrb_avail[0] = do_pcibr_rrb_count_avail(bridge, 0);
    pcibr_soft->bs_rrb_avail[1] = do_pcibr_rrb_count_avail(bridge, 1);

    /*
     * Calculate reserved RRBs for slots based on current RRB usage
     */
    for (eo = 0; eo < 2; eo++) {
        if ((3 * have[eo][0] + 2 * have[eo][1] + have[eo][2]) <= pcibr_soft->bs_rrb_avail[eo])
            res[eo] = 3;
        else if ((2 * have[eo][0] + have[eo][1]) <= pcibr_soft->bs_rrb_avail[eo])
            res[eo] = 2;
        else if (have[eo][0] <= pcibr_soft->bs_rrb_avail[eo])
            res[eo] = 1;
        else
            res[eo] = 0;

    }

    /* Assign reserved RRBs to existing slots */
    for (slot = first; slot <= last; ++slot) {
        int                     r;

        c0 = pcibr_soft->bs_rrb_valid[slot];
        c1 = pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL];
        r = res[slot & 1] - (c0 + c1);

        if (r > 0) {
            pcibr_soft->bs_rrb_res[slot] = r;
            pcibr_soft->bs_rrb_avail[slot & 1] -= r;
            }
    }

#if PCIBR_RRB_DEBUG
    printk("%v RRB MANAGEMENT: %d+%d free\n",
            pcibr_vhdl,
            pcibr_soft->bs_rrb_avail[0],
            pcibr_soft->bs_rrb_avail[1]);
    for (slot = first; slot <= last; ++slot)
        printk("\tslot %d: %d+%d+%d", slot,
                0xFFF & pcibr_soft->bs_rrb_valid[slot],
                0xFFF & pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL],
                pcibr_soft->bs_rrb_res[slot]);
    printk("\n");
#endif

    return 0;

}

