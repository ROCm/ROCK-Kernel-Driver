/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/interrupt.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/iograph.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>
#include <asm/sn/pci/pic.h>
#include <asm/sn/sn_private.h>

extern struct file_operations pcibr_fops;
extern pcibr_list_p	pcibr_list;

static int		pic_attach2(vertex_hdl_t, void *, vertex_hdl_t,
 				int, pcibr_soft_t *);

extern int		isIO9(nasid_t);
extern char	       *dev_to_name(vertex_hdl_t dev, char *buf, uint buflen);
extern int		pcibr_widget_to_bus(vertex_hdl_t pcibr_vhdl);
extern pcibr_hints_t	pcibr_hints_get(vertex_hdl_t, int);
extern unsigned		pcibr_intr_bits(pciio_info_t info,
				pciio_intr_line_t lines, int nslots);
extern void		pcibr_setwidint(xtalk_intr_t);
extern int		pcibr_error_handler_wrapper(error_handler_arg_t, int,
				ioerror_mode_t, ioerror_t *);
extern void		pcibr_error_intr_handler(intr_arg_t);
extern void		pcibr_directmap_init(pcibr_soft_t);
extern int		pcibr_slot_info_init(vertex_hdl_t,pciio_slot_t);
extern int		pcibr_slot_addr_space_init(vertex_hdl_t,pciio_slot_t);
extern int		pcibr_slot_device_init(vertex_hdl_t, pciio_slot_t);
extern int		pcibr_slot_pcix_rbar_init(pcibr_soft_t, pciio_slot_t);
extern int		pcibr_slot_guest_info_init(vertex_hdl_t,pciio_slot_t);
extern int		pcibr_slot_call_device_attach(vertex_hdl_t,
				pciio_slot_t, int);
extern void		pcibr_rrb_alloc_init(pcibr_soft_t, int, int, int);
extern int		pcibr_pcix_rbars_calc(pcibr_soft_t);
extern pcibr_info_t	pcibr_device_info_new(pcibr_soft_t, pciio_slot_t,
				pciio_function_t, pciio_vendor_id_t,
				pciio_device_id_t);
extern int		pcibr_initial_rrb(vertex_hdl_t, pciio_slot_t, 
				pciio_slot_t);
extern void		xwidget_error_register(vertex_hdl_t, error_handler_f *,
				error_handler_arg_t);
extern void		pcibr_clearwidint(pcibr_soft_t);



/*
 * copy xwidget_info_t from conn_v to peer_conn_v
 */
static int
pic_bus1_widget_info_dup(vertex_hdl_t conn_v, vertex_hdl_t peer_conn_v,
					cnodeid_t xbow_peer, char *peer_path)
{
	xwidget_info_t widget_info, peer_widget_info;
	vertex_hdl_t peer_hubv;
	hubinfo_t peer_hub_info;

	/* get the peer hub's widgetid */
	peer_hubv = NODEPDA(xbow_peer)->node_vertex;
	peer_hub_info = NULL;
	hubinfo_get(peer_hubv, &peer_hub_info);
	if (peer_hub_info == NULL)
		return 0;

	if (hwgraph_info_get_LBL(conn_v, INFO_LBL_XWIDGET,
			(arbitrary_info_t *)&widget_info) == GRAPH_SUCCESS) {
		peer_widget_info = kmalloc(sizeof (*(peer_widget_info)), GFP_KERNEL);
		if ( !peer_widget_info ) {
			return -ENOMEM;
		}
		memset(peer_widget_info, 0, sizeof (*(peer_widget_info)));

		peer_widget_info->w_fingerprint = widget_info_fingerprint;
    		peer_widget_info->w_vertex = peer_conn_v;
    		peer_widget_info->w_id = widget_info->w_id;
    		peer_widget_info->w_master = peer_hubv;
    		peer_widget_info->w_masterid = peer_hub_info->h_widgetid;
		/* structure copy */
    		peer_widget_info->w_hwid = widget_info->w_hwid;
    		peer_widget_info->w_efunc = 0;
    		peer_widget_info->w_einfo = 0;
		peer_widget_info->w_name = kmalloc(strlen(peer_path) + 1, GFP_KERNEL);
		if (!peer_widget_info->w_name) {
			kfree(peer_widget_info);
			return -ENOMEM;
		}
		strcpy(peer_widget_info->w_name, peer_path);

		if (hwgraph_info_add_LBL(peer_conn_v, INFO_LBL_XWIDGET,
			(arbitrary_info_t)peer_widget_info) != GRAPH_SUCCESS) {
			kfree(peer_widget_info->w_name);
				kfree(peer_widget_info);
				return 0;
		}

		xwidget_info_set(peer_conn_v, peer_widget_info);

		return 1;
	}

	printk("pic_bus1_widget_info_dup: "
			"cannot get INFO_LBL_XWIDGET from 0x%lx\n", (uint64_t)conn_v);
	return 0;
}

/*
 * If this PIC is attached to two Cbricks ("dual-ported") then
 * attach each bus to opposite Cbricks.
 *
 * If successful, return a new vertex suitable for attaching the PIC bus.
 * If not successful, return zero and both buses will attach to the
 * vertex passed into pic_attach().
 */
static vertex_hdl_t
pic_bus1_redist(nasid_t nasid, vertex_hdl_t conn_v)
{
	cnodeid_t cnode = nasid_to_cnodeid(nasid);
	cnodeid_t xbow_peer = -1;
	char pathname[256], peer_path[256], tmpbuf[256];
	char *p;
	int rc;
	vertex_hdl_t peer_conn_v, hubv;
	int pos;
	slabid_t slab;

	if (NODEPDA(cnode)->xbow_peer >= 0) {			/* if dual-ported */
		/* create a path for this widget on the peer Cbrick */
		/* pcibr widget hw/module/001c11/slab/0/Pbrick/xtalk/12 */
		/* sprintf(pathname, "%v", conn_v); */
		xbow_peer = nasid_to_cnodeid(NODEPDA(cnode)->xbow_peer);
		pos = hwgfs_generate_path(conn_v, tmpbuf, 256);
		strcpy(pathname, &tmpbuf[pos]);
		p = pathname + strlen("hw/module/001c01/slab/0/");

		memset(tmpbuf, 0, 16);
		format_module_id(tmpbuf, geo_module((NODEPDA(xbow_peer))->geoid), MODULE_FORMAT_BRIEF);
		slab = geo_slab((NODEPDA(xbow_peer))->geoid);
		sprintf(peer_path, "module/%s/slab/%d/%s", tmpbuf, (int)slab, p); 
		
		/* Look for vertex for this widget on the peer Cbrick.
		 * Expect GRAPH_NOT_FOUND.
		 */
		rc = hwgraph_traverse(hwgraph_root, peer_path, &peer_conn_v);
		if (GRAPH_SUCCESS == rc)
			printk("pic_attach: found unexpected vertex: 0x%lx\n",
								(uint64_t)peer_conn_v);
		else if (GRAPH_NOT_FOUND != rc) {
			printk("pic_attach: hwgraph_traverse unexpectedly"
					" returned 0x%x\n", rc);
		} else {
			/* try to add the widget vertex to the peer Cbrick */
			rc = hwgraph_path_add(hwgraph_root, peer_path, &peer_conn_v);

			if (GRAPH_SUCCESS != rc)
			    printk("pic_attach: hwgraph_path_add"
						" failed with 0x%x\n", rc);
			else {
			    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ATTACH, conn_v,
					"pic_bus1_redist: added vertex %v\n", peer_conn_v)); 

			    /* Now hang appropiate stuff off of the new
			     * vertex.	We bail out if we cannot add something.
			     * In that case, we don't remove the newly added
			     * vertex but that should be safe and we don't
			     * really expect the additions to fail anyway.
			     */
			    if (!pic_bus1_widget_info_dup(conn_v, peer_conn_v, 
							  xbow_peer, peer_path))
					return 0;

			    hubv = cnodeid_to_vertex(xbow_peer);
			    ASSERT(hubv != GRAPH_VERTEX_NONE);
			    device_master_set(peer_conn_v, hubv);
			    xtalk_provider_register(hubv, &hub_provider);
			    xtalk_provider_startup(hubv);
			    return peer_conn_v;
			}
		}
	}
	return 0;
}

/*
 * PIC has two buses under a single widget.  pic_attach() calls pic_attach2()
 * to attach each of those buses.
 */
int
pic_attach(vertex_hdl_t conn_v)
{
	int		rc;
	void	*bridge0, *bridge1 = (void *)0;
	vertex_hdl_t	pcibr_vhdl0, pcibr_vhdl1 = (vertex_hdl_t)0;
	pcibr_soft_t	bus0_soft, bus1_soft = (pcibr_soft_t)0;
	vertex_hdl_t  conn_v0, conn_v1, peer_conn_v;
	int		bricktype;
	int		iobrick_type_get_nasid(nasid_t nasid);

	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ATTACH, conn_v, "pic_attach()\n"));

	bridge0 = pcibr_bridge_ptr_get(conn_v, 0);
	bridge1 = pcibr_bridge_ptr_get(conn_v, 1);

	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ATTACH, conn_v,
		    "pic_attach: bridge0=0x%lx, bridge1=0x%lx\n", 
		    bridge0, bridge1));

	conn_v0 = conn_v1 = conn_v;

	/* If dual-ported then split the two PIC buses across both Cbricks */
	peer_conn_v = pic_bus1_redist(NASID_GET(bridge0), conn_v);
	if (peer_conn_v)
		conn_v1 = peer_conn_v;

	/*
	 * Create the vertex for the PCI buses, which we
	 * will also use to hold the pcibr_soft and
	 * which will be the "master" vertex for all the
	 * pciio connection points we will hang off it.
	 * This needs to happen before we call nic_bridge_vertex_info
	 * as we are some of the *_vmc functions need access to the edges.
	 *
	 * Opening this vertex will provide access to
	 * the Bridge registers themselves.
	 */
	bricktype = iobrick_type_get_nasid(NASID_GET(bridge0));
	if ( bricktype == MODULE_CGBRICK ) {
		rc = hwgraph_path_add(conn_v0, EDGE_LBL_AGP_0, &pcibr_vhdl0);
		ASSERT(rc == GRAPH_SUCCESS);
		rc = hwgraph_path_add(conn_v1, EDGE_LBL_AGP_1, &pcibr_vhdl1);
		ASSERT(rc == GRAPH_SUCCESS);
	} else {
		rc = hwgraph_path_add(conn_v0, EDGE_LBL_PCIX_0, &pcibr_vhdl0);
		ASSERT(rc == GRAPH_SUCCESS);
		rc = hwgraph_path_add(conn_v1, EDGE_LBL_PCIX_1, &pcibr_vhdl1);
		ASSERT(rc == GRAPH_SUCCESS);
	}

	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ATTACH, conn_v,
		    "pic_attach: pcibr_vhdl0=0x%lx, pcibr_vhdl1=0x%lx\n",
		    pcibr_vhdl0, pcibr_vhdl1));

	/* register pci provider array */
	pciio_provider_register(pcibr_vhdl0, &pci_pic_provider);
	pciio_provider_register(pcibr_vhdl1, &pci_pic_provider);

	pciio_provider_startup(pcibr_vhdl0);
	pciio_provider_startup(pcibr_vhdl1);

	pic_attach2(conn_v0, bridge0, pcibr_vhdl0, 0, &bus0_soft);
	pic_attach2(conn_v1, bridge1, pcibr_vhdl1, 1, &bus1_soft);

	{
	    /* If we're dual-ported finish duplicating the peer info structure.
	     * The error handler and arg are done in pic_attach2().
	     */
	    xwidget_info_t info0, info1;
		if (conn_v0 != conn_v1) {	/* dual ported */
			info0 = xwidget_info_get(conn_v0);
			info1 = xwidget_info_get(conn_v1);
			if (info1->w_efunc == (error_handler_f *)NULL)
				info1->w_efunc = info0->w_efunc;
			if (info1->w_einfo == (error_handler_arg_t)0)
				info1->w_einfo = bus1_soft;
		}
	}

	/* save a pointer to the PIC's other bus's soft struct */
        bus0_soft->bs_peers_soft = bus1_soft;
        bus1_soft->bs_peers_soft = bus0_soft;

	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ATTACH, conn_v,
		    "pic_attach: bus0_soft=0x%lx, bus1_soft=0x%lx\n",
		    bus0_soft, bus1_soft));

	return 0;
}


/*
 * PIC has two buses under a single widget.  pic_attach() calls pic_attach2()
 * to attach each of those buses.
 */
static int
pic_attach2(vertex_hdl_t xconn_vhdl, void *bridge,
	      vertex_hdl_t pcibr_vhdl, int busnum, pcibr_soft_t *ret_softp)
{
    vertex_hdl_t	    ctlr_vhdl;
    pcibr_soft_t	    pcibr_soft;
    pcibr_info_t	    pcibr_info;
    xwidget_info_t	    info;
    xtalk_intr_t	    xtalk_intr;
    pcibr_list_p	    self;
    int			    entry, slot, ibit, i;
    vertex_hdl_t	    noslot_conn;
    char		    devnm[MAXDEVNAME], *s;
    pcibr_hints_t	    pcibr_hints;
    picreg_t		    id;
    picreg_t		    int_enable;
    picreg_t		    pic_ctrl_reg;

    int			    iobrick_type_get_nasid(nasid_t nasid);
    int			    iomoduleid_get(nasid_t nasid);
    int			    irq;
    int			    cpu;

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ATTACH, pcibr_vhdl,
		"pic_attach2: bridge=0x%lx, busnum=%d\n", bridge, busnum));

    ctlr_vhdl = NULL;
    ctlr_vhdl = hwgraph_register(pcibr_vhdl, EDGE_LBL_CONTROLLER, 0,
		0, 0, 0,
		S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP, 0, 0,
		(struct file_operations *)&pcibr_fops, (void *)pcibr_vhdl);
    ASSERT(ctlr_vhdl != NULL);

    id = pcireg_bridge_id_get(bridge);
    hwgraph_info_add_LBL(pcibr_vhdl, INFO_LBL_PCIBR_ASIC_REV,
                         (arbitrary_info_t)XWIDGET_PART_REV_NUM(id));

    /*
     * Get the hint structure; if some NIC callback marked this vertex as
     * "hands-off" then we just return here, before doing anything else.
     */
    pcibr_hints = pcibr_hints_get(xconn_vhdl, 0);

    if (pcibr_hints && pcibr_hints->ph_hands_off)
        return -1;

    /* allocate soft structure to hang off the vertex.  Link the new soft
     * structure to the pcibr_list linked list
     */
    pcibr_soft = kmalloc(sizeof (*(pcibr_soft)), GFP_KERNEL);
    if ( !pcibr_soft )
	return -ENOMEM;

    self = kmalloc(sizeof (*(self)), GFP_KERNEL);
    if ( !self ) {
	kfree(pcibr_soft);
	return -ENOMEM;
    }
    memset(pcibr_soft, 0, sizeof (*(pcibr_soft)));
    memset(self, 0, sizeof (*(self)));

    self->bl_soft = pcibr_soft;
    self->bl_vhdl = pcibr_vhdl;
    self->bl_next = pcibr_list;
    pcibr_list = self;

    if (ret_softp)
        *ret_softp = pcibr_soft;

    memset(pcibr_soft, 0, sizeof *pcibr_soft);
    pcibr_soft_set(pcibr_vhdl, pcibr_soft);

    s = dev_to_name(pcibr_vhdl, devnm, MAXDEVNAME);
    pcibr_soft->bs_name = kmalloc(strlen(s) + 1, GFP_KERNEL);
    if (!pcibr_soft->bs_name)
	    return -ENOMEM;

    strcpy(pcibr_soft->bs_name, s);

    pcibr_soft->bs_conn = xconn_vhdl;
    pcibr_soft->bs_vhdl = pcibr_vhdl;
    pcibr_soft->bs_base = (void *)bridge;
    pcibr_soft->bs_rev_num = XWIDGET_PART_REV_NUM(id);
    pcibr_soft->bs_intr_bits = (pcibr_intr_bits_f *)pcibr_intr_bits;
    pcibr_soft->bsi_err_intr = 0;
    pcibr_soft->bs_min_slot = 0;
    pcibr_soft->bs_max_slot = 3;
    pcibr_soft->bs_busnum = busnum;
    pcibr_soft->bs_bridge_type = PCIBR_BRIDGETYPE_PIC;
    pcibr_soft->bs_int_ate_size = PIC_INTERNAL_ATES;
    /* Make sure this is called after setting the bs_base and bs_bridge_type */
    pcibr_soft->bs_bridge_mode = (pcireg_speed_get(pcibr_soft) << 1) |
                                  pcireg_mode_get(pcibr_soft);

    info = xwidget_info_get(xconn_vhdl);
    pcibr_soft->bs_xid = xwidget_info_id_get(info);
    pcibr_soft->bs_master = xwidget_info_master_get(info);
    pcibr_soft->bs_mxid = xwidget_info_masterid_get(info);

    strcpy(pcibr_soft->bs_asic_name, "PIC");

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ATTACH, pcibr_vhdl,
                "pic_attach2: pcibr_soft=0x%lx, mode=0x%x\n",
                pcibr_soft, pcibr_soft->bs_bridge_mode));

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ATTACH, pcibr_vhdl,
                "pic_attach2: %s ASIC: rev %s (code=0x%x)\n",
                pcibr_soft->bs_asic_name,
                (IS_PIC_PART_REV_A(pcibr_soft->bs_rev_num)) ? "A" :
                (IS_PIC_PART_REV_B(pcibr_soft->bs_rev_num)) ? "B" :
                (IS_PIC_PART_REV_C(pcibr_soft->bs_rev_num)) ? "C" :
                "unknown", pcibr_soft->bs_rev_num));

    /* PV854845: Must clear write request buffer to avoid parity errors */
    for (i=0; i < PIC_WR_REQ_BUFSIZE; i++) {
        ((pic_t *)bridge)->p_wr_req_lower[i] = 0;
        ((pic_t *)bridge)->p_wr_req_upper[i] = 0;
        ((pic_t *)bridge)->p_wr_req_parity[i] = 0;
    }

    pcibr_soft->bs_nasid = NASID_GET(bridge);

    pcibr_soft->bs_bricktype = iobrick_type_get_nasid(pcibr_soft->bs_nasid);
    if (pcibr_soft->bs_bricktype < 0)
        printk(KERN_WARNING "%s: bricktype was unknown by L1 (ret val = 0x%x)\n",
                pcibr_soft->bs_name, pcibr_soft->bs_bricktype);

    pcibr_soft->bs_moduleid = iomoduleid_get(pcibr_soft->bs_nasid);

    if (pcibr_soft->bs_bricktype > 0) {
        switch (pcibr_soft->bs_bricktype) {
	case MODULE_PXBRICK:
	case MODULE_IXBRICK:
	case MODULE_OPUSBRICK:
            pcibr_soft->bs_first_slot = 0;
            pcibr_soft->bs_last_slot = 1;
            pcibr_soft->bs_last_reset = 1;

            /* Bus 1 of IXBrick has a IO9, so there are 4 devices, not 2 */
	    if ((pcibr_widget_to_bus(pcibr_vhdl) == 1) 
		    && isIO9(pcibr_soft->bs_nasid)) {
                pcibr_soft->bs_last_slot = 3;
                pcibr_soft->bs_last_reset = 3;
            }
            break;

        case MODULE_CGBRICK:
            pcibr_soft->bs_first_slot = 0;
            pcibr_soft->bs_last_slot = 0;
            pcibr_soft->bs_last_reset = 0;
            break;

        default:
	    printk(KERN_WARNING "%s: Unknown bricktype: 0x%x\n",
                    pcibr_soft->bs_name, pcibr_soft->bs_bricktype);
            break;
        }

        PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ATTACH, pcibr_vhdl,
                    "pic_attach2: bricktype=%d, brickbus=%d, "
		    "slots %d-%d\n", pcibr_soft->bs_bricktype,
		    pcibr_widget_to_bus(pcibr_vhdl),
                    pcibr_soft->bs_first_slot, pcibr_soft->bs_last_slot));
    }

    /*
     * Initialize bridge and bus locks
     */
    spin_lock_init(&pcibr_soft->bs_lock);

    /*
     * If we have one, process the hints structure.
     */
    if (pcibr_hints) {
        unsigned	rrb_fixed;
        PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_HINTS, pcibr_vhdl,
                    "pic_attach2: pcibr_hints=0x%lx\n", pcibr_hints));

        rrb_fixed = pcibr_hints->ph_rrb_fixed;

        pcibr_soft->bs_rrb_fixed = rrb_fixed;

        if (pcibr_hints->ph_intr_bits)
            pcibr_soft->bs_intr_bits = pcibr_hints->ph_intr_bits;


        for (slot = pcibr_soft->bs_min_slot;
                                slot < PCIBR_NUM_SLOTS(pcibr_soft); ++slot) {
            int hslot = pcibr_hints->ph_host_slot[slot] - 1;

            if (hslot < 0) {
                pcibr_soft->bs_slot[slot].host_slot = slot;
            } else {
                pcibr_soft->bs_slot[slot].has_host = 1;
                pcibr_soft->bs_slot[slot].host_slot = hslot;
            }
        }
    }

    /*
     * Set-up initial values for state fields
     */
    for (slot = pcibr_soft->bs_min_slot;
                                slot < PCIBR_NUM_SLOTS(pcibr_soft); ++slot) {
        pcibr_soft->bs_slot[slot].bss_devio.bssd_space = PCIIO_SPACE_NONE;
        pcibr_soft->bs_slot[slot].bss_devio.bssd_ref_cnt = 0;
        pcibr_soft->bs_slot[slot].bss_d64_base = PCIBR_D64_BASE_UNSET;
        pcibr_soft->bs_slot[slot].bss_d32_base = PCIBR_D32_BASE_UNSET;
        pcibr_soft->bs_rrb_valid_dflt[slot][VCHAN0] = -1;
    }

    for (ibit = 0; ibit < 8; ++ibit) {
        pcibr_soft->bs_intr[ibit].bsi_xtalk_intr = 0;
        pcibr_soft->bs_intr[ibit].bsi_pcibr_intr_wrap.iw_soft = pcibr_soft;
        pcibr_soft->bs_intr[ibit].bsi_pcibr_intr_wrap.iw_list = NULL;
        pcibr_soft->bs_intr[ibit].bsi_pcibr_intr_wrap.iw_ibit = ibit;
        pcibr_soft->bs_intr[ibit].bsi_pcibr_intr_wrap.iw_hdlrcnt = 0;
        pcibr_soft->bs_intr[ibit].bsi_pcibr_intr_wrap.iw_shared = 0;
        pcibr_soft->bs_intr[ibit].bsi_pcibr_intr_wrap.iw_connected = 0;
    }


    /*
     * connect up our error handler.  PIC has 2 busses (thus resulting in 2
     * pcibr_soft structs under 1 widget), so only register a xwidget error
     * handler for PIC's bus0.  NOTE: for PIC pcibr_error_handler_wrapper()
     * is a wrapper routine we register that will call the real error handler
     * pcibr_error_handler() with the correct pcibr_soft struct.
     */
    if (busnum == 0) {
        xwidget_error_register(xconn_vhdl,
                                pcibr_error_handler_wrapper, pcibr_soft);
    }

    /*
     * Clear all pending interrupts.  Assume all interrupts are from slot 3
     * until otherise setup.
     */
    pcireg_intr_reset_set(pcibr_soft, PIC_IRR_ALL_CLR);
    pcireg_intr_device_set(pcibr_soft, 0x006db6db);

    /* Setup the mapping register used for direct mapping */
    pcibr_directmap_init(pcibr_soft);

    /*
     * Initialize the PICs control register.
     */
    pic_ctrl_reg = pcireg_control_get(pcibr_soft);

    /* Bridges Requester ID: bus = busnum, dev = 0, func = 0 */
    pic_ctrl_reg &= ~PIC_CTRL_BUS_NUM_MASK;
    pic_ctrl_reg |= PIC_CTRL_BUS_NUM(busnum);
    pic_ctrl_reg &= ~PIC_CTRL_DEV_NUM_MASK;
    pic_ctrl_reg &= ~PIC_CTRL_FUN_NUM_MASK;

    pic_ctrl_reg &= ~PIC_CTRL_NO_SNOOP;
    pic_ctrl_reg &= ~PIC_CTRL_RELAX_ORDER;

    /* enable parity checking on PICs internal RAM */
    pic_ctrl_reg |= PIC_CTRL_PAR_EN_RESP;
    pic_ctrl_reg |= PIC_CTRL_PAR_EN_ATE;

    /* PIC BRINGUP WAR (PV# 862253): dont enable write request parity */
    if (!PCIBR_WAR_ENABLED(PV862253, pcibr_soft)) {
        pic_ctrl_reg |= PIC_CTRL_PAR_EN_REQ;
    }

    pic_ctrl_reg |= PIC_CTRL_PAGE_SIZE;

    pcireg_control_set(pcibr_soft, pic_ctrl_reg);

    /* Initialize internal mapping entries (ie. the ATEs) */
    for (entry = 0; entry < pcibr_soft->bs_int_ate_size; entry++)
	pcireg_int_ate_set(pcibr_soft, entry, 0);

    pcibr_soft->bs_int_ate_resource.start = 0;
    pcibr_soft->bs_int_ate_resource.end = pcibr_soft->bs_int_ate_size - 1;

    /* Setup the PICs error interrupt handler. */
    xtalk_intr = xtalk_intr_alloc(xconn_vhdl, (device_desc_t)0, pcibr_vhdl);

    ASSERT(xtalk_intr != NULL);

    irq = ((hub_intr_t)xtalk_intr)->i_bit;
    cpu = ((hub_intr_t)xtalk_intr)->i_cpuid;

    intr_unreserve_level(cpu, irq);
    ((hub_intr_t)xtalk_intr)->i_bit = SGI_PCIBR_ERROR;
    xtalk_intr->xi_vector = SGI_PCIBR_ERROR;

    pcibr_soft->bsi_err_intr = xtalk_intr;

    /*
     * On IP35 with XBridge, we do some extra checks in pcibr_setwidint
     * in order to work around some addressing limitations.  In order
     * for that fire wall to work properly, we need to make sure we
     * start from a known clean state.
     */
    pcibr_clearwidint(pcibr_soft);

    xtalk_intr_connect(xtalk_intr,
		       (intr_func_t) pcibr_error_intr_handler,
		       (intr_arg_t) pcibr_soft,
		       (xtalk_intr_setfunc_t) pcibr_setwidint,
		       (void *) pcibr_soft);

    request_irq(SGI_PCIBR_ERROR, (void *)pcibr_error_intr_handler, SA_SHIRQ, 
			"PCIBR error", (intr_arg_t) pcibr_soft);

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pcibr_vhdl,
		"pcibr_setwidint: target_id=0x%lx, int_addr=0x%lx\n",
		pcireg_intr_dst_target_id_get(pcibr_soft),
		pcireg_intr_dst_addr_get(pcibr_soft)));

    /* now we can start handling error interrupts */
    int_enable = pcireg_intr_enable_get(pcibr_soft);
    int_enable |= PIC_ISR_ERRORS;

    /* PIC BRINGUP WAR (PV# 856864 & 856865): allow the tnums that are
     * locked out to be freed up sooner (by timing out) so that the
     * read tnums are never completely used up.
     */
    if (PCIBR_WAR_ENABLED(PV856864, pcibr_soft)) {
	int_enable &= ~PIC_ISR_PCIX_REQ_TOUT;
	int_enable &= ~PIC_ISR_XREAD_REQ_TIMEOUT;

	pcireg_req_timeout_set(pcibr_soft, 0x750);
    }

    pcireg_intr_enable_set(pcibr_soft, int_enable);
    pcireg_intr_mode_set(pcibr_soft, 0); /* dont send 'clear interrupt' pkts */
    pcireg_tflush_get(pcibr_soft);       /* wait until Bridge PIO complete */

    /*
     * PIC BRINGUP WAR (PV# 856866, 859504, 861476, 861478): Don't use
     * RRB0, RRB8, RRB1, and RRB9.  Assign them to DEVICE[2|3]--VCHAN3
     * so they are not used.  This works since there is currently no
     * API to penable VCHAN3.
     */
    if (PCIBR_WAR_ENABLED(PV856866, pcibr_soft)) {
	pcireg_rrb_bit_set(pcibr_soft, 0, 0x000f000f);	/* even rrb reg */
	pcireg_rrb_bit_set(pcibr_soft, 1, 0x000f000f);	/* odd rrb reg */
    }

    /* PIC only supports 64-bit direct mapping in PCI-X mode.  Since
     * all PCI-X devices that initiate memory transactions must be
     * capable of generating 64-bit addressed, we force 64-bit DMAs.
     */
    pcibr_soft->bs_dma_flags = 0;
    if (IS_PCIX(pcibr_soft)) {
	pcibr_soft->bs_dma_flags |= PCIIO_DMA_A64;
    }

    {

    iopaddr_t		    prom_base_addr = pcibr_soft->bs_xid << 24;
    int			    prom_base_size = 0x1000000;
    int			    status;
    struct resource	    *res;

    /* Allocate resource maps based on bus page size; for I/O and memory
     * space, free all pages except those in the base area and in the
     * range set by the PROM.
     *
     * PROM creates BAR addresses in this format: 0x0ws00000 where w is
     * the widget number and s is the device register offset for the slot.
     */

    /* Setup the Bus's PCI IO Root Resource. */
    pcibr_soft->bs_io_win_root_resource.start = PCIBR_BUS_IO_BASE;
    pcibr_soft->bs_io_win_root_resource.end = 0xffffffff;
    res = (struct resource *) kmalloc( sizeof(struct resource), GFP_KERNEL);
    if (!res)
	panic("PCIBR:Unable to allocate resource structure\n");

    /* Block off the range used by PROM. */
    res->start = prom_base_addr;
    res->end = prom_base_addr + (prom_base_size - 1);
    status = request_resource(&pcibr_soft->bs_io_win_root_resource, res);
    if (status)
	panic("PCIBR:Unable to request_resource()\n");

    /* Setup the Small Window Root Resource */
    pcibr_soft->bs_swin_root_resource.start = PAGE_SIZE;
    pcibr_soft->bs_swin_root_resource.end = 0x000FFFFF;

    /* Setup the Bus's PCI Memory Root Resource */
    pcibr_soft->bs_mem_win_root_resource.start = 0x200000;
    pcibr_soft->bs_mem_win_root_resource.end = 0xffffffff;
    res = (struct resource *) kmalloc( sizeof(struct resource), GFP_KERNEL);
    if (!res)
	panic("PCIBR:Unable to allocate resource structure\n");

    /* Block off the range used by PROM. */
    res->start = prom_base_addr;
    res->end = prom_base_addr + (prom_base_size - 1);
    status = request_resource(&pcibr_soft->bs_mem_win_root_resource, res);
    if (status)
	panic("PCIBR:Unable to request_resource()\n");

    }


    /* build "no-slot" connection point */
    pcibr_info = pcibr_device_info_new(pcibr_soft, PCIIO_SLOT_NONE,
		 PCIIO_FUNC_NONE, PCIIO_VENDOR_ID_NONE, PCIIO_DEVICE_ID_NONE);
    noslot_conn = pciio_device_info_register(pcibr_vhdl, &pcibr_info->f_c);

    /* Store no slot connection point info for tearing it down during detach. */
    pcibr_soft->bs_noslot_conn = noslot_conn;
    pcibr_soft->bs_noslot_info = pcibr_info;

    for (slot = pcibr_soft->bs_min_slot;
				slot < PCIBR_NUM_SLOTS(pcibr_soft); ++slot) {
	/* Find out what is out there */
	(void)pcibr_slot_info_init(pcibr_vhdl, slot);
    }

    for (slot = pcibr_soft->bs_min_slot;
				slot < PCIBR_NUM_SLOTS(pcibr_soft); ++slot) {
	/* Set up the address space for this slot in the PCI land */
	(void)pcibr_slot_addr_space_init(pcibr_vhdl, slot);
    }

    for (slot = pcibr_soft->bs_min_slot;
				slot < PCIBR_NUM_SLOTS(pcibr_soft); ++slot) {
	/* Setup the device register */
	(void)pcibr_slot_device_init(pcibr_vhdl, slot);
    }

    if (IS_PCIX(pcibr_soft)) {
	pcibr_soft->bs_pcix_rbar_inuse = 0;
	pcibr_soft->bs_pcix_rbar_avail = NUM_RBAR;
	pcibr_soft->bs_pcix_rbar_percent_allowed =
					pcibr_pcix_rbars_calc(pcibr_soft);

	for (slot = pcibr_soft->bs_min_slot;
				slot < PCIBR_NUM_SLOTS(pcibr_soft); ++slot) {
	    /* Setup the PCI-X Read Buffer Attribute Registers (RBARs) */
	    (void)pcibr_slot_pcix_rbar_init(pcibr_soft, slot);
	}
    }

    for (slot = pcibr_soft->bs_min_slot;
				slot < PCIBR_NUM_SLOTS(pcibr_soft); ++slot) {
	/* Setup host/guest relations */
	(void)pcibr_slot_guest_info_init(pcibr_vhdl, slot);
    }

    /* Handle initial RRB management */
    pcibr_initial_rrb(pcibr_vhdl,
		      pcibr_soft->bs_first_slot, pcibr_soft->bs_last_slot);

   /* Before any drivers get called that may want to re-allocate RRB's,
    * let's get some special cases pre-allocated. Drivers may override
    * these pre-allocations, but by doing pre-allocations now we're
    * assured not to step all over what the driver intended.
    */
    if (pcibr_soft->bs_bricktype > 0) {
	switch (pcibr_soft->bs_bricktype) {
	case MODULE_PXBRICK:
	case MODULE_IXBRICK:
	case MODULE_OPUSBRICK:
		/*
		 * If IO9 in bus 1, allocate RRBs to all the IO9 devices
		 */
		if ((pcibr_widget_to_bus(pcibr_vhdl) == 1) &&
		    (pcibr_soft->bs_slot[0].bss_vendor_id == 0x10A9) &&
		    (pcibr_soft->bs_slot[0].bss_device_id == 0x100A)) {
			pcibr_rrb_alloc_init(pcibr_soft, 0, VCHAN0, 4);
			pcibr_rrb_alloc_init(pcibr_soft, 1, VCHAN0, 4);
			pcibr_rrb_alloc_init(pcibr_soft, 2, VCHAN0, 4);
			pcibr_rrb_alloc_init(pcibr_soft, 3, VCHAN0, 4);
		} else {
			pcibr_rrb_alloc_init(pcibr_soft, 0, VCHAN0, 4);
			pcibr_rrb_alloc_init(pcibr_soft, 1, VCHAN0, 4);
		}
		break;

	case MODULE_CGBRICK:
		pcibr_rrb_alloc_init(pcibr_soft, 0, VCHAN0, 8);
		break;
	} /* switch */
    }


    for (slot = pcibr_soft->bs_min_slot;
				slot < PCIBR_NUM_SLOTS(pcibr_soft); ++slot) {
	/* Call the device attach */
	(void)pcibr_slot_call_device_attach(pcibr_vhdl, slot, 0);
    }

    pciio_device_attach(noslot_conn, 0);

    return 0;
}


/*
 * pci provider functions
 *
 * mostly in pcibr.c but if any are needed here then
 * this might be a way to get them here.
 */
pciio_provider_t        pci_pic_provider =
{
    PCIIO_ASIC_TYPE_PIC,

    (pciio_piomap_alloc_f *) pcibr_piomap_alloc,
    (pciio_piomap_free_f *) pcibr_piomap_free,
    (pciio_piomap_addr_f *) pcibr_piomap_addr,
    (pciio_piomap_done_f *) pcibr_piomap_done,
    (pciio_piotrans_addr_f *) pcibr_piotrans_addr,
    (pciio_piospace_alloc_f *) pcibr_piospace_alloc,
    (pciio_piospace_free_f *) pcibr_piospace_free,

    (pciio_dmamap_alloc_f *) pcibr_dmamap_alloc,
    (pciio_dmamap_free_f *) pcibr_dmamap_free,
    (pciio_dmamap_addr_f *) pcibr_dmamap_addr,
    (pciio_dmamap_done_f *) pcibr_dmamap_done,
    (pciio_dmatrans_addr_f *) pcibr_dmatrans_addr,
    (pciio_dmamap_drain_f *) pcibr_dmamap_drain,
    (pciio_dmaaddr_drain_f *) pcibr_dmaaddr_drain,

    (pciio_intr_alloc_f *) pcibr_intr_alloc,
    (pciio_intr_free_f *) pcibr_intr_free,
    (pciio_intr_connect_f *) pcibr_intr_connect,
    (pciio_intr_disconnect_f *) pcibr_intr_disconnect,
    (pciio_intr_cpu_get_f *) pcibr_intr_cpu_get,

    (pciio_provider_startup_f *) pcibr_provider_startup,
    (pciio_provider_shutdown_f *) pcibr_provider_shutdown,
    (pciio_reset_f *) pcibr_reset,
    (pciio_endian_set_f *) pcibr_endian_set,
    (pciio_config_get_f *) pcibr_config_get,
    (pciio_config_set_f *) pcibr_config_set,

    (pciio_error_extract_f *) pcibr_error_extract,

    (pciio_driver_reg_callback_f *) pcibr_driver_reg_callback,
    (pciio_driver_unreg_callback_f *) pcibr_driver_unreg_callback,
    (pciio_device_unregister_f 	*) pcibr_device_unregister,
};
