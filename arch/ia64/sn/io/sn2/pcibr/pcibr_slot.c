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

extern pcibr_info_t     pcibr_info_get(devfs_handle_t);
extern int              pcibr_widget_to_bus(int);
extern pcibr_info_t     pcibr_device_info_new(pcibr_soft_t, pciio_slot_t, pciio_function_t, pciio_vendor_id_t, pciio_device_id_t);
extern void             pcibr_freeblock_sub(iopaddr_t *, iopaddr_t *, iopaddr_t, size_t);
extern int		pcibr_slot_initial_rrb_alloc(devfs_handle_t,pciio_slot_t);
#if 0
int pcibr_slot_reset(devfs_handle_t pcibr_vhdl, pciio_slot_t slot);
#endif

int pcibr_slot_info_init(devfs_handle_t pcibr_vhdl, pciio_slot_t slot);
int pcibr_slot_info_free(devfs_handle_t pcibr_vhdl, pciio_slot_t slot);
int pcibr_slot_addr_space_init(devfs_handle_t pcibr_vhdl,  pciio_slot_t slot);
int pcibr_slot_device_init(devfs_handle_t pcibr_vhdl,  pciio_slot_t slot);
int pcibr_slot_guest_info_init(devfs_handle_t pcibr_vhdl,  pciio_slot_t slot);
int pcibr_slot_call_device_attach(devfs_handle_t pcibr_vhdl,
		 pciio_slot_t slot, int drv_flags);
int pcibr_slot_call_device_detach(devfs_handle_t pcibr_vhdl,
		 pciio_slot_t slot, int drv_flags);
int pcibr_slot_detach(devfs_handle_t pcibr_vhdl, pciio_slot_t slot, int drv_flags);
int pcibr_is_slot_sys_critical(devfs_handle_t pcibr_vhdl, pciio_slot_t slot);
int pcibr_probe_slot(bridge_t *, cfg_p, unsigned int *);
void		 pcibr_device_info_free(devfs_handle_t, pciio_slot_t);
extern uint64_t  do_pcibr_config_get(cfg_p, unsigned, unsigned);

#ifdef LATER
int pcibr_slot_attach(devfs_handle_t pcibr_vhdl, pciio_slot_t slot,
                int drv_flags, char *l1_msg, int *sub_errorp);
int pcibr_slot_pwr(devfs_handle_t, pciio_slot_t, int, char *);
int pcibr_slot_startup(devfs_handle_t, pcibr_slot_req_t);
int pcibr_slot_shutdown(devfs_handle_t, pcibr_slot_req_t);
void pcibr_slot_func_info_return(pcibr_info_h pcibr_infoh, int func,
                 pcibr_slot_func_info_resp_t funcp);
int pcibr_slot_info_return(pcibr_soft_t pcibr_soft, pciio_slot_t slot,
                 pcibr_slot_info_resp_t respp);
int pcibr_slot_query(devfs_handle_t, pcibr_slot_req_t);
#endif	/* LATER */

extern devfs_handle_t baseio_pci_vhdl;
int scsi_ctlr_nums_add(devfs_handle_t, devfs_handle_t);

/* For now .... */
/*
 * PCI Hot-Plug Capability Flags
 */
#define D_PCI_HOT_PLUG_ATTACH  0x200  /* Driver supports PCI hot-plug attach */
#define D_PCI_HOT_PLUG_DETACH  0x400  /* Driver supports PCI hot-plug detach */


/*==========================================================================
 *	BRIDGE PCI SLOT RELATED IOCTLs
 */

#ifdef LATER

/*
 * pcibr_slot_startup
 *	Software start-up the PCI slot.
 */
int
pcibr_slot_startup(devfs_handle_t pcibr_vhdl, pcibr_slot_req_t reqp)
{
    pcibr_soft_t                   pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    pciio_slot_t                   slot = reqp->req_slot;
    int                            error = 0;
    char                           l1_msg[BRL1_QSIZE+1];
    struct pcibr_slot_up_resp_s    tmp_up_resp;

    /* Make sure that we are dealing with a bridge device vertex */
    if (!pcibr_soft) {
        return(PCI_NOT_A_BRIDGE);
    }

    /* Do not allow start-up of a slot in a shoehorn */
    if(nic_vertex_info_match(pcibr_soft->bs_conn, XTALK_PCI_PART_NUM)) {
       return(PCI_SLOT_IN_SHOEHORN);
    }
 
    /* Check for the valid slot */
    if (!PCIBR_VALID_SLOT(slot))
        return(PCI_NOT_A_SLOT);

    /* Acquire update access to the bus */
    mrlock(pcibr_soft->bs_bus_lock, MR_UPDATE, PZERO);

    if (pcibr_soft->bs_slot[slot].slot_status & SLOT_STARTUP_CMPLT) {
        error = PCI_SLOT_ALREADY_UP;
        goto startup_unlock;
    }

    error = pcibr_slot_attach(pcibr_vhdl, slot, D_PCI_HOT_PLUG_ATTACH,
                              l1_msg, &tmp_up_resp.resp_sub_errno);

    strncpy(tmp_up_resp.resp_l1_msg, l1_msg, L1_QSIZE);
    tmp_up_resp.resp_l1_msg[L1_QSIZE] = '\0';

    if (COPYOUT(&tmp_up_resp, reqp->req_respp.up, reqp->req_size)) {
        return(EFAULT);
    }

    startup_unlock:

    /* Release the bus lock */
    mrunlock(pcibr_soft->bs_bus_lock);

    return(error);
}

/*
 * pcibr_slot_shutdown
 *	Software shut-down the PCI slot
 */
int
pcibr_slot_shutdown(devfs_handle_t pcibr_vhdl, pcibr_slot_req_t reqp)
{
    pcibr_soft_t                   pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    bridge_t                      *bridge;
    pciio_slot_t                   slot = reqp->req_slot;
    int                            error = 0;
    char                           l1_msg[BRL1_QSIZE+1];
    struct pcibr_slot_down_resp_s  tmp_down_resp;
    pciio_slot_t                   tmp_slot;

    /* Make sure that we are dealing with a bridge device vertex */
    if (!pcibr_soft) {
        return(PCI_NOT_A_BRIDGE);
    }

    bridge = pcibr_soft->bs_base;

    /* Check for valid slot */
    if (!PCIBR_VALID_SLOT(slot))
        return(PCI_NOT_A_SLOT);

    /* Do not allow shut-down of a slot in a shoehorn */
    if(nic_vertex_info_match(pcibr_soft->bs_conn, XTALK_PCI_PART_NUM)) {
       return(PCI_SLOT_IN_SHOEHORN);
    }

    /* Acquire update access to the bus */
    mrlock(pcibr_soft->bs_bus_lock, MR_UPDATE, PZERO);

    if ((pcibr_soft->bs_slot[slot].slot_status & SLOT_SHUTDOWN_CMPLT) ||
        ((pcibr_soft->bs_slot[slot].slot_status & SLOT_STATUS_MASK) == 0)) {
        error = PCI_SLOT_ALREADY_DOWN;
        /*
         * RJR - Should we invoke an L1 slot power-down command just in case
         *       a previous shut-down failed to power-down the slot?
         */
        goto shutdown_unlock;
    }

    /* Do not allow the last 33 MHz card to be removed */
    if ((bridge->b_wid_control & BRIDGE_CTRL_BUS_SPEED_MASK) ==
         BRIDGE_CTRL_BUS_SPEED_33) {
        for (tmp_slot = pcibr_soft->bs_first_slot;
             tmp_slot <= pcibr_soft->bs_last_slot; tmp_slot++)
            if (tmp_slot != slot)
                if (pcibr_soft->bs_slot[tmp_slot].slot_status & SLOT_POWER_UP) {
                    error++;
                    break;
                }
        if (!error) {
            error = PCI_EMPTY_33MHZ;
            goto shutdown_unlock;
        }
    }

    error = pcibr_slot_detach(pcibr_vhdl, slot, D_PCI_HOT_PLUG_DETACH,
                              l1_msg, &tmp_down_resp.resp_sub_errno);

    strncpy(tmp_down_resp.resp_l1_msg, l1_msg, L1_QSIZE);
    tmp_down_resp.resp_l1_msg[L1_QSIZE] = '\0';

    if (COPYOUT(&tmp_down_resp, reqp->req_respp.down, reqp->req_size)) {
        return(EFAULT);
    }

    shutdown_unlock:

    /* Release the bus lock */
    mrunlock(pcibr_soft->bs_bus_lock);

    return(error);
}

char *pci_space_name[] = {"NONE", 
			  "ROM",
			  "IO",
			  "",
			  "MEM",
			  "MEM32",
			  "MEM64",
			  "CFG",
			  "WIN0",
			  "WIN1",
			  "WIN2",
			  "WIN3",
			  "WIN4",
			  "WIN5",
			  "",
			  "BAD"};

void
pcibr_slot_func_info_return(pcibr_info_h pcibr_infoh,
                            int func,
                            pcibr_slot_func_info_resp_t funcp)
{
    pcibr_info_t                 pcibr_info = pcibr_infoh[func];
    int                          win;

    funcp->resp_f_status = 0;

    if (!pcibr_info) {
        return;
    }

    funcp->resp_f_status |= FUNC_IS_VALID;
    sprintf(funcp->resp_f_slot_name, "%v", pcibr_info->f_vertex);

    if(is_sys_critical_vertex(pcibr_info->f_vertex)) {
        funcp->resp_f_status |= FUNC_IS_SYS_CRITICAL;
    }

    funcp->resp_f_bus = pcibr_info->f_bus;
    funcp->resp_f_slot = pcibr_info->f_slot;
    funcp->resp_f_func = pcibr_info->f_func;
    sprintf(funcp->resp_f_master_name, "%v", pcibr_info->f_master);
    funcp->resp_f_pops = pcibr_info->f_pops;
    funcp->resp_f_efunc = pcibr_info->f_efunc;
    funcp->resp_f_einfo = pcibr_info->f_einfo;

    funcp->resp_f_vendor = pcibr_info->f_vendor;
    funcp->resp_f_device = pcibr_info->f_device;

    for(win = 0 ; win < 6 ; win++) {
        funcp->resp_f_window[win].resp_w_base =
                                  pcibr_info->f_window[win].w_base;
        funcp->resp_f_window[win].resp_w_size =
                                  pcibr_info->f_window[win].w_size;
        sprintf(funcp->resp_f_window[win].resp_w_space,
                "%s",
                pci_space_name[pcibr_info->f_window[win].w_space]);
    }

    funcp->resp_f_rbase = pcibr_info->f_rbase;
    funcp->resp_f_rsize = pcibr_info->f_rsize;

    for (win = 0 ; win < 4; win++) {
        funcp->resp_f_ibit[win] = pcibr_info->f_ibit[win];
    }

    funcp->resp_f_att_det_error = pcibr_info->f_att_det_error;

}

int
pcibr_slot_info_return(pcibr_soft_t             pcibr_soft,
                       pciio_slot_t             slot,
                       pcibr_slot_info_resp_t   respp)
{
    pcibr_soft_slot_t            pss;
    int                          func;
    bridge_t                    *bridge = pcibr_soft->bs_base;
    reg_p                        b_respp;
    pcibr_slot_info_resp_t       slotp;
    pcibr_slot_func_info_resp_t  funcp;

    slotp = kmem_zalloc(sizeof(*slotp), KM_SLEEP);
    if (slotp == NULL) {
        return(ENOMEM);
    }

    pss = &pcibr_soft->bs_slot[slot];

    slotp->resp_has_host = pss->has_host;
    slotp->resp_host_slot = pss->host_slot;
    sprintf(slotp->resp_slot_conn_name, "%v", pss->slot_conn);
    slotp->resp_slot_status = pss->slot_status;

    slotp->resp_l1_bus_num = io_path_map_widget(pcibr_soft->bs_vhdl);

    if (is_sys_critical_vertex(pss->slot_conn)) {
        slotp->resp_slot_status |= SLOT_IS_SYS_CRITICAL;
    }

    slotp->resp_bss_ninfo = pss->bss_ninfo;

    for (func = 0; func < pss->bss_ninfo; func++) {
        funcp = &(slotp->resp_func[func]);
        pcibr_slot_func_info_return(pss->bss_infos, func, funcp);
    }

    sprintf(slotp->resp_bss_devio_bssd_space, "%s",
            pci_space_name[pss->bss_devio.bssd_space]);
    slotp->resp_bss_devio_bssd_base = pss->bss_devio.bssd_base;
    slotp->resp_bss_device = pss->bss_device;

    slotp->resp_bss_pmu_uctr = pss->bss_pmu_uctr;
    slotp->resp_bss_d32_uctr = pss->bss_d32_uctr;
    slotp->resp_bss_d64_uctr = pss->bss_d64_uctr;

    slotp->resp_bss_d64_base = pss->bss_d64_base;
    slotp->resp_bss_d64_flags = pss->bss_d64_flags;
    slotp->resp_bss_d32_base = pss->bss_d32_base;
    slotp->resp_bss_d32_flags = pss->bss_d32_flags;

    slotp->resp_bss_ext_ates_active = pss->bss_ext_ates_active;

    slotp->resp_bss_cmd_pointer = pss->bss_cmd_pointer;
    slotp->resp_bss_cmd_shadow = pss->bss_cmd_shadow;

    slotp->resp_bs_rrb_valid = pcibr_soft->bs_rrb_valid[slot];
    slotp->resp_bs_rrb_valid_v = pcibr_soft->bs_rrb_valid[slot +
                                                      PCIBR_RRB_SLOT_VIRTUAL];
    slotp->resp_bs_rrb_res = pcibr_soft->bs_rrb_res[slot];

    if (slot & 1) {
        b_respp = &bridge->b_odd_resp;
    } else {
        b_respp = &bridge->b_even_resp;
    }

    slotp->resp_b_resp = *b_respp;

    slotp->resp_b_wid_control = bridge->b_wid_control;
    slotp->resp_b_int_device = bridge->b_int_device;
    slotp->resp_b_int_enable = bridge->b_int_enable;
    slotp->resp_b_int_host = bridge->b_int_addr[slot].addr;

    if (COPYOUT(slotp, respp, sizeof(*respp))) {
        return(EFAULT);
    }

    kmem_free(slotp, sizeof(*slotp));

    return(0);
}

/*
 * pcibr_slot_query
 *	Return information about the PCI slot maintained by the infrastructure.
 *	Information is requested in the request structure.
 *
 *      Information returned in the response structure:
 *		Slot hwgraph name
 *		Vendor/Device info
 *		Base register info
 *		Interrupt mapping from device pins to the bridge pins
 *		Devio register
 *		Software RRB info
 *		RRB register info
 *		Host/Gues info
 *		PCI Bus #,slot #, function #
 *		Slot provider hwgraph name
 *		Provider Functions
 *		Error handler
 *		DMA mapping usage counters
 *		DMA direct translation info
 *		External SSRAM workaround info
 */
int
pcibr_slot_query(devfs_handle_t pcibr_vhdl, pcibr_slot_req_t reqp)
{
    pcibr_soft_t            pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    pciio_slot_t            slot = reqp->req_slot;
    pciio_slot_t            tmp_slot;
    pcibr_slot_info_resp_t  respp = reqp->req_respp.query;
    int                     size = reqp->req_size;
    int                     error;

    /* Make sure that we are dealing with a bridge device vertex */
    if (!pcibr_soft) {
        return(PCI_NOT_A_BRIDGE);
    }

    /* Make sure that we have a valid PCI slot number or PCIIO_SLOT_NONE */
    if ((!PCIBR_VALID_SLOT(slot)) && (slot != PCIIO_SLOT_NONE)) {
        return(PCI_NOT_A_SLOT);
    }

    /* Do not allow a query of a slot in a shoehorn */
    if(nic_vertex_info_match(pcibr_soft->bs_conn, XTALK_PCI_PART_NUM)) {
       return(PCI_SLOT_IN_SHOEHORN);
    }

    /* Return information for the requested PCI slot */
    if (slot != PCIIO_SLOT_NONE) {
        if (size < sizeof(*respp)) {
            return(PCI_RESP_AREA_TOO_SMALL);
        }

        /* Acquire read access to the bus */
        mrlock(pcibr_soft->bs_bus_lock, MR_ACCESS, PZERO);

        error = pcibr_slot_info_return(pcibr_soft, slot, respp);

        /* Release the bus lock */
        mrunlock(pcibr_soft->bs_bus_lock);

        return(error);
    }

    /* Return information for all the slots */
    for (tmp_slot = 0; tmp_slot < 8; tmp_slot++) {

        if (size < sizeof(*respp)) {
            return(PCI_RESP_AREA_TOO_SMALL);
        }

        /* Acquire read access to the bus */
        mrlock(pcibr_soft->bs_bus_lock, MR_ACCESS, PZERO);

        error = pcibr_slot_info_return(pcibr_soft, tmp_slot, respp);

        /* Release the bus lock */
        mrunlock(pcibr_soft->bs_bus_lock);

        if (error) {
            return(error);
        }

        ++respp;
        size -= sizeof(*respp);
    }

    return(error);
}
#endif	/* LATER */

/* FIXME: there should be a better way to do this.
 * pcibr_attach() needs PCI_ADDR_SPACE_LIMITS_STORE
 */

/* 
 * PCI_ADDR_SPACE_LIMITS_LOAD
 *	Gets the current values of 
 *		pci io base, 
 *		pci io last,
 *		pci low memory base,
 *		pci low memory last,
 *		pci high memory base,
 * 		pci high memory last
 */
#define PCI_ADDR_SPACE_LIMITS_LOAD()			\
    pci_io_fb = pcibr_soft->bs_spinfo.pci_io_base;	\
    pci_io_fl = pcibr_soft->bs_spinfo.pci_io_last;	\
    pci_lo_fb = pcibr_soft->bs_spinfo.pci_swin_base;	\
    pci_lo_fl = pcibr_soft->bs_spinfo.pci_swin_last;	\
    pci_hi_fb = pcibr_soft->bs_spinfo.pci_mem_base;	\
    pci_hi_fl = pcibr_soft->bs_spinfo.pci_mem_last;
/*
 * PCI_ADDR_SPACE_LIMITS_STORE
 *	Sets the current values of
 *		pci io base, 
 *		pci io last,
 *		pci low memory base,
 *		pci low memory last,
 *		pci high memory base,
 * 		pci high memory last
 */
#define PCI_ADDR_SPACE_LIMITS_STORE()			\
    pcibr_soft->bs_spinfo.pci_io_base = pci_io_fb;	\
    pcibr_soft->bs_spinfo.pci_io_last = pci_io_fl;	\
    pcibr_soft->bs_spinfo.pci_swin_base = pci_lo_fb;	\
    pcibr_soft->bs_spinfo.pci_swin_last = pci_lo_fl;	\
    pcibr_soft->bs_spinfo.pci_mem_base = pci_hi_fb;	\
    pcibr_soft->bs_spinfo.pci_mem_last = pci_hi_fl;

#define PCI_ADDR_SPACE_LIMITS_PRINT()			\
    printf("+++++++++++++++++++++++\n"			\
	   "IO base 0x%x last 0x%x\n"			\
	   "SWIN base 0x%x last 0x%x\n"			\
	   "MEM base 0x%x last 0x%x\n"			\
	   "+++++++++++++++++++++++\n",			\
	   pcibr_soft->bs_spinfo.pci_io_base,		\
	   pcibr_soft->bs_spinfo.pci_io_last,		\
	   pcibr_soft->bs_spinfo.pci_swin_base,		\
	   pcibr_soft->bs_spinfo.pci_swin_last,		\
	   pcibr_soft->bs_spinfo.pci_mem_base,		\
	   pcibr_soft->bs_spinfo.pci_mem_last);


/*
 * pcibr_slot_info_init
 *	Probe for this slot and see if it is populated.
 *	If it is populated initialize the generic PCI infrastructural
 * 	information associated with this particular PCI device.
 */
int
pcibr_slot_info_init(devfs_handle_t 	pcibr_vhdl,
		     pciio_slot_t 	slot)
{
    pcibr_soft_t	    pcibr_soft;
    pcibr_info_h	    pcibr_infoh;
    pcibr_info_t	    pcibr_info;
    bridge_t		   *bridge;
    cfg_p                   cfgw;
    unsigned                idword;
    unsigned                pfail;
    unsigned                idwords[8];
    pciio_vendor_id_t       vendor;
    pciio_device_id_t       device;
    unsigned                htype;
    cfg_p                   wptr;
    int                     win;
    pciio_space_t           space;
    iopaddr_t		    pci_io_fb,	pci_io_fl;
    iopaddr_t		    pci_lo_fb,  pci_lo_fl;
    iopaddr_t		    pci_hi_fb,  pci_hi_fl;
    int			    nfunc;
    pciio_function_t	    rfunc;
    int			    func;
    devfs_handle_t	    conn_vhdl;
    pcibr_soft_slot_t	    slotp;
    
    /* Get the basic software information required to proceed */
    pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    if (!pcibr_soft)
	return(EINVAL);

    bridge = pcibr_soft->bs_base;
    if (!PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    /* If we have a host slot (eg:- IOC3 has 2 PCI slots and the initialization
     * is done by the host slot then we are done.
     */
    if (pcibr_soft->bs_slot[slot].has_host) {
	return(0);    
    }

    /* Check for a slot with any system critical functions */
    if (pcibr_is_slot_sys_critical(pcibr_vhdl, slot))
        return(EPERM);

    /* Load the current values of allocated PCI address spaces */
    PCI_ADDR_SPACE_LIMITS_LOAD();
    
    /* Try to read the device-id/vendor-id from the config space */
    cfgw = bridge->b_type0_cfg_dev[slot].l;

    if (pcibr_probe_slot(bridge, cfgw, &idword)) 
	return(ENODEV);

    slotp = &pcibr_soft->bs_slot[slot];
    slotp->slot_status |= SLOT_POWER_UP;

    vendor = 0xFFFF & idword;
    /* If the vendor id is not valid then the slot is not populated
     * and we are done.
     */
    if (vendor == 0xFFFF) 
	return(ENODEV);			
    
    device = 0xFFFF & (idword >> 16);
    htype = do_pcibr_config_get(cfgw, PCI_CFG_HEADER_TYPE, 1);

    nfunc = 1;
    rfunc = PCIIO_FUNC_NONE;
    pfail = 0;

    /* NOTE: if a card claims to be multifunction
     * but only responds to config space 0, treat
     * it as a unifunction card.
     */

    if (htype & 0x80) {		/* MULTIFUNCTION */
	for (func = 1; func < 8; ++func) {
	    cfgw = bridge->b_type0_cfg_dev[slot].f[func].l;
	    if (pcibr_probe_slot(bridge, cfgw, &idwords[func])) {
		pfail |= 1 << func;
		continue;
	    }
	    vendor = 0xFFFF & idwords[func];
	    if (vendor == 0xFFFF) {
		pfail |= 1 << func;
		continue;
	    }
	    nfunc = func + 1;
	    rfunc = 0;
	}
	cfgw = bridge->b_type0_cfg_dev[slot].l;
    }
    NEWA(pcibr_infoh, nfunc);
    
    pcibr_soft->bs_slot[slot].bss_ninfo = nfunc;
    pcibr_soft->bs_slot[slot].bss_infos = pcibr_infoh;

    for (func = 0; func < nfunc; ++func) {
	unsigned                cmd_reg;
	
	if (func) {
	    if (pfail & (1 << func))
		continue;
	    
	    idword = idwords[func];
	    cfgw = bridge->b_type0_cfg_dev[slot].f[func].l;
	    
	    device = 0xFFFF & (idword >> 16);
	    htype = do_pcibr_config_get(cfgw, PCI_CFG_HEADER_TYPE, 1);
	    rfunc = func;
	}
	htype &= 0x7f;
	if (htype != 0x00) {
	    printk(KERN_WARNING  "%s pcibr: pci slot %d func %d has strange header type 0x%x\n",
		    pcibr_soft->bs_name, slot, func, htype);
	    continue;
	}
#if DEBUG && ATTACH_DEBUG
	printk(KERN_NOTICE   
		"%s pcibr: pci slot %d func %d: vendor 0x%x device 0x%x",
		pcibr_soft->bs_name, slot, func, vendor, device);
#endif	

	pcibr_info = pcibr_device_info_new
	    (pcibr_soft, slot, rfunc, vendor, device);
	conn_vhdl = pciio_device_info_register(pcibr_vhdl, &pcibr_info->f_c);
	if (func == 0)
	    slotp->slot_conn = conn_vhdl;

#ifdef LITTLE_ENDIAN
	cmd_reg = cfgw[(PCI_CFG_COMMAND ^ 4) / 4];
#else
	cmd_reg = cfgw[PCI_CFG_COMMAND / 4];
#endif
	
	wptr = cfgw + PCI_CFG_BASE_ADDR_0 / 4;

	for (win = 0; win < PCI_CFG_BASE_ADDRS; ++win) {
	    iopaddr_t               base, mask, code;
	    size_t                  size;

	    /*
	     * GET THE BASE & SIZE OF THIS WINDOW:
	     *
	     * The low two or four bits of the BASE register
	     * determines which address space we are in; the
	     * rest is a base address. BASE registers
	     * determine windows that are power-of-two sized
	     * and naturally aligned, so we can get the size
	     * of a window by writing all-ones to the
	     * register, reading it back, and seeing which
	     * bits are used for decode; the least
	     * significant nonzero bit is also the size of
	     * the window.
	     *
	     * WARNING: someone may already have allocated
	     * some PCI space to this window, and in fact
	     * PIO may be in process at this very moment
	     * from another processor (or even from this
	     * one, if we get interrupted)! So, if the BASE
	     * already has a nonzero address, be generous
	     * and use the LSBit of that address as the
	     * size; this could overstate the window size.
	     * Usually, when one card is set up, all are set
	     * up; so, since we don't bitch about
	     * overlapping windows, we are ok.
	     *
	     * UNFORTUNATELY, some cards do not clear their
	     * BASE registers on reset. I have two heuristics
	     * that can detect such cards: first, if the
	     * decode enable is turned off for the space
	     * that the window uses, we can disregard the
	     * initial value. second, if the address is
	     * outside the range that we use, we can disregard
	     * it as well.
	     *
	     * This is looking very PCI generic. Except for
	     * knowing how many slots and where their config
	     * spaces are, this window loop and the next one
	     * could probably be shared with other PCI host
	     * adapters. It would be interesting to see if
	     * this could be pushed up into pciio, when we
	     * start supporting more PCI providers.
	     */
#ifdef LITTLE_ENDIAN
	    base = wptr[((win*4)^4)/4];
#else
	    base = wptr[win];
#endif

	    if (base & PCI_BA_IO_SPACE) {
		/* BASE is in I/O space. */
		space = PCIIO_SPACE_IO;
		mask = -4;
		code = base & 3;
		base = base & mask;
		if (base == 0) {
		    ;		/* not assigned */
		} else if (!(cmd_reg & PCI_CMD_IO_SPACE)) {
		    base = 0;	/* decode not enabled */
		}
	    } else {
		/* BASE is in MEM space. */
		space = PCIIO_SPACE_MEM;
		mask = -16;
		code = base & PCI_BA_MEM_LOCATION;	/* extract BAR type */
		base = base & mask;
		if (base == 0) {
		    ;		/* not assigned */
		} else if (!(cmd_reg & PCI_CMD_MEM_SPACE)) {
		    base = 0;	/* decode not enabled */
		} else if (base & 0xC0000000) {
		    base = 0;	/* outside permissable range */
		} else if ((code == PCI_BA_MEM_64BIT) &&
#ifdef LITTLE_ENDIAN
			   (wptr[(((win + 1)*4)^4)/4] != 0)) {
#else 
			   (wptr[win + 1] != 0)) {
#endif /* LITTLE_ENDIAN */
		    base = 0;	/* outside permissable range */
		}
	    }

	    if (base != 0) {	/* estimate size */
		size = base & -base;
	    } else {		/* calculate size */
#ifdef LITTLE_ENDIAN
		wptr[((win*4)^4)/4] = ~0;	/* turn on all bits */
		size = wptr[((win*4)^4)/4];	/* get stored bits */
#else 
		wptr[win] = ~0;	/* turn on all bits */
		size = wptr[win];	/* get stored bits */
#endif /* LITTLE_ENDIAN */
		size &= mask;	/* keep addr */
		size &= -size;	/* keep lsbit */
		if (size == 0)
		    continue;
	    }	

	    pcibr_info->f_window[win].w_space = space;
	    pcibr_info->f_window[win].w_base = base;
	    pcibr_info->f_window[win].w_size = size;

	    /*
	     * If this window already has PCI space
	     * allocated for it, "subtract" that space from
	     * our running freeblocks. Don't worry about
	     * overlaps in existing allocated windows; we
	     * may be overstating their sizes anyway.
	     */

	    if (base && size) {
		if (space == PCIIO_SPACE_IO) {
		    pcibr_freeblock_sub(&pci_io_fb,
					&pci_io_fl,
					base, size);
		} else {
		    pcibr_freeblock_sub(&pci_lo_fb,
					&pci_lo_fl,
					base, size);
		    pcibr_freeblock_sub(&pci_hi_fb,
					&pci_hi_fl,
					base, size);
		}	
	    }
#if defined(IOC3_VENDOR_ID_NUM) && defined(IOC3_DEVICE_ID_NUM)
	    /*
	     * IOC3 BASE_ADDR* BUG WORKAROUND
	     *
	     
	     * If we write to BASE1 on the IOC3, the
	     * data in BASE0 is replaced. The
	     * original workaround was to remember
	     * the value of BASE0 and restore it
	     * when we ran off the end of the BASE
	     * registers; however, a later
	     * workaround was added (I think it was
	     * rev 1.44) to avoid setting up
	     * anything but BASE0, with the comment
	     * that writing all ones to BASE1 set
	     * the enable-parity-error test feature
	     * in IOC3's SCR bit 14.
	     *
	     * So, unless we defer doing any PCI
	     * space allocation until drivers
	     * attach, and set up a way for drivers
	     * (the IOC3 in paricular) to tell us
	     * generically to keep our hands off
	     * BASE registers, we gotta "know" about
	     * the IOC3 here.
	     *
	     * Too bad the PCI folks didn't reserve the
	     * all-zero value for 'no BASE here' (it is a
	     * valid code for an uninitialized BASE in
	     * 32-bit PCI memory space).
	     */
	    
	    if ((vendor == IOC3_VENDOR_ID_NUM) &&
		(device == IOC3_DEVICE_ID_NUM))
		break;
#endif
	    if (code == PCI_BA_MEM_64BIT) {
		win++;		/* skip upper half */
#ifdef LITTLE_ENDIAN
		wptr[((win*4)^4)/4] = 0;	/* which must be zero */
#else 
		wptr[win] = 0;	/* which must be zero */
#endif /* LITTLE_ENDIAN */
	    }
	}				/* next win */
    }				/* next func */

    /* Store back the values for allocated PCI address spaces */
    PCI_ADDR_SPACE_LIMITS_STORE();
    return(0);
}					

/*
 * pcibr_slot_info_free
 *	Remove all the PCI infrastructural information associated
 * 	with a particular PCI device.
 */
int
pcibr_slot_info_free(devfs_handle_t pcibr_vhdl,
                     pciio_slot_t slot)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    int			nfunc;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;

    pcibr_device_info_free(pcibr_vhdl, slot);

    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;
    DELA(pcibr_infoh,nfunc);
    pcibr_soft->bs_slot[slot].bss_ninfo = 0;

    return(0);
}

int as_debug = 0;
/*
 * pcibr_slot_addr_space_init
 *	Reserve chunks of PCI address space as required by 
 * 	the base registers in the card.
 */
int
pcibr_slot_addr_space_init(devfs_handle_t pcibr_vhdl,
			   pciio_slot_t	slot)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    pcibr_info_t	pcibr_info;
    bridge_t		*bridge;
    iopaddr_t		pci_io_fb, pci_io_fl;
    iopaddr_t		pci_lo_fb, pci_lo_fl;
    iopaddr_t		pci_hi_fb, pci_hi_fl;
    size_t              align;
    iopaddr_t           mask;
    int		    	nbars;
    int		       	nfunc;
    int			func;
    int			win;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    bridge = pcibr_soft->bs_base;

    /* Get the current values for the allocated PCI address spaces */
    PCI_ADDR_SPACE_LIMITS_LOAD();

    if (as_debug)
#ifdef LATER
    PCI_ADDR_SPACE_LIMITS_PRINT();
#endif

    /* allocate address space,
     * for windows that have not been
     * previously assigned.
     */
    if (pcibr_soft->bs_slot[slot].has_host) {
	return(0);
    }

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;
    if (nfunc < 1)
	return(EINVAL);

    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;
    if (!pcibr_infoh)
	return(EINVAL);

    /*
     * Try to make the DevIO windows not
     * overlap by pushing the "io" and "hi"
     * allocation areas up to the next one
     * or two megabyte bound. This also
     * keeps them from being zero.
     *
     * DO NOT do this with "pci_lo" since
     * the entire "lo" area is only a
     * megabyte, total ...
     */
    align = (slot < 2) ? 0x200000 : 0x100000;
    mask = -align;
    pci_io_fb = (pci_io_fb + align - 1) & mask;
    pci_hi_fb = (pci_hi_fb + align - 1) & mask;

    for (func = 0; func < nfunc; ++func) {
	cfg_p                   cfgw;
	cfg_p                   wptr;
	pciio_space_t           space;
	iopaddr_t               base;
	size_t                  size;
	cfg_p                   pci_cfg_cmd_reg_p;
	unsigned                pci_cfg_cmd_reg;
	unsigned                pci_cfg_cmd_reg_add = 0;

	pcibr_info = pcibr_infoh[func];

	if (!pcibr_info)
	    continue;

	if (pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE)
	    continue;
	
	cfgw = bridge->b_type0_cfg_dev[slot].f[func].l;
	wptr = cfgw + PCI_CFG_BASE_ADDR_0 / 4;

	nbars = PCI_CFG_BASE_ADDRS;

	for (win = 0; win < nbars; ++win) {

	    space = pcibr_info->f_window[win].w_space;
	    base = pcibr_info->f_window[win].w_base;
	    size = pcibr_info->f_window[win].w_size;
	    
	    if (size < 1)
		continue;

	    if (base >= size) {
#if DEBUG && PCI_DEBUG
		printk("pcibr: slot %d func %d window %d is in %d[0x%x..0x%x], alloc by prom\n",
			slot, func, win, space, base, base + size - 1);
#endif
		continue;		/* already allocated */
	    }
	    align = size;		/* ie. 0x00001000 */
	    if (align < _PAGESZ)
		align = _PAGESZ;	/* ie. 0x00004000 */
	    mask = -align;		/* ie. 0xFFFFC000 */

	    switch (space) {
	    case PCIIO_SPACE_IO:
		base = (pci_io_fb + align - 1) & mask;
		if ((base + size) > pci_io_fl) {
		    base = 0;
		    break;
		}
		pci_io_fb = base + size;
		break;
		
	    case PCIIO_SPACE_MEM:
#ifdef LITTLE_ENDIAN
		if ((wptr[((win*4)^4)/4] & PCI_BA_MEM_LOCATION) ==
#else
		if ((wptr[win] & PCI_BA_MEM_LOCATION) ==
#endif  /* LITTLE_ENDIAN */
		    PCI_BA_MEM_1MEG) {
		    /* allocate from 20-bit PCI space */
		    base = (pci_lo_fb + align - 1) & mask;
		    if ((base + size) > pci_lo_fl) {
			base = 0;
			break;
		    }
		    pci_lo_fb = base + size;
		} else {
		    /* allocate from 32-bit or 64-bit PCI space */
		    base = (pci_hi_fb + align - 1) & mask;
		    if ((base + size) > pci_hi_fl) {
			base = 0;
			break;
		    }
		    pci_hi_fb = base + size;
		}
		break;
		
	    default:
		base = 0;
#if DEBUG && PCI_DEBUG
		printk("pcibr: slot %d window %d had bad space code %d\n",
			slot, win, space);
#endif
	    }
	    pcibr_info->f_window[win].w_base = base;
#ifdef LITTLE_ENDIAN
	    wptr[((win*4)^4)/4] = base;
#if DEBUG && PCI_DEBUG
		printk("Setting base address 0x%p base 0x%x\n", &(wptr[((win*4)^4)/4]), base);
#endif
#else
	    wptr[win] = base;
#endif  /* LITTLE_ENDIAN */

#if DEBUG && PCI_DEBUG
	    if (base >= size)
		printk("pcibr: slot %d func %d window %d is in %d [0x%x..0x%x], alloc by pcibr\n",
			slot, func, win, space, base, base + size - 1);
	    else
		printk("pcibr: slot %d func %d window %d, unable to alloc 0x%x in 0x%p\n",
			slot, func, win, size, space);
#endif
	}				/* next base */

	/*
	 * Allocate space for the EXPANSION ROM
	 * NOTE: DO NOT DO THIS ON AN IOC3,
	 * as it blows the system away.
	 */
	base = size = 0;
	if ((pcibr_soft->bs_slot[slot].bss_vendor_id != IOC3_VENDOR_ID_NUM) ||
	    (pcibr_soft->bs_slot[slot].bss_device_id != IOC3_DEVICE_ID_NUM)) {

	    wptr = cfgw + PCI_EXPANSION_ROM / 4;
#ifdef LITTLE_ENDIAN
	    wptr[1] = 0xFFFFF000;
	    mask = wptr[1];
#else
	    *wptr = 0xFFFFF000;
	    mask = *wptr;
#endif  /* LITTLE_ENDIAN */
	    if (mask & 0xFFFFF000) {
		size = mask & -mask;
		align = size;
		if (align < _PAGESZ)
		    align = _PAGESZ;
		mask = -align;
		base = (pci_hi_fb + align - 1) & mask;
		if ((base + size) > pci_hi_fl)
		    base = size = 0;
		else {
		    pci_hi_fb = base + size;
#ifdef LITTLE_ENDIAN
		    wptr[1] = base;
#else
		    *wptr = base;
#endif  /* LITTLE_ENDIAN */
#if DEBUG && PCI_DEBUG
		    printk("%s/%d ROM in 0x%lx..0x%lx (alloc by pcibr)\n",
			    pcibr_soft->bs_name, slot,
			    base, base + size - 1);
#endif
		}
	    }
	}
	pcibr_info->f_rbase = base;
	pcibr_info->f_rsize = size;
	
	/*
	 * if necessary, update the board's
	 * command register to enable decoding
	 * in the windows we added.
	 *
	 * There are some bits we always want to
	 * be sure are set.
	 */
	pci_cfg_cmd_reg_add |= PCI_CMD_IO_SPACE;

	/*
	 * The Adaptec 1160 FC Controller WAR #767995:
	 * The part incorrectly ignores the upper 32 bits of a 64 bit
	 * address when decoding references to it's registers so to
	 * keep it from responding to a bus cycle that it shouldn't
	 * we only use I/O space to get at it's registers.  Don't
	 * enable memory space accesses on that PCI device.
	 */
	#define FCADP_VENDID 0x9004 /* Adaptec Vendor ID from fcadp.h */
	#define FCADP_DEVID 0x1160  /* Adaptec 1160 Device ID from fcadp.h */

	if ((pcibr_info->f_vendor != FCADP_VENDID) ||
	    (pcibr_info->f_device != FCADP_DEVID))
	    pci_cfg_cmd_reg_add |= PCI_CMD_MEM_SPACE;

	pci_cfg_cmd_reg_add |= PCI_CMD_BUS_MASTER;

	pci_cfg_cmd_reg_p = cfgw + PCI_CFG_COMMAND / 4;
	pci_cfg_cmd_reg = *pci_cfg_cmd_reg_p;
#if PCI_FBBE	/* XXX- check here to see if dev can do fast-back-to-back */
	if (!((pci_cfg_cmd_reg >> 16) & PCI_STAT_F_BK_BK_CAP))
	    fast_back_to_back_enable = 0;
#endif
	pci_cfg_cmd_reg &= 0xFFFF;
	if (pci_cfg_cmd_reg_add & ~pci_cfg_cmd_reg)
	    *pci_cfg_cmd_reg_p = pci_cfg_cmd_reg | pci_cfg_cmd_reg_add;
	
    }				/* next func */

    /* Now that we have allocated new chunks of PCI address spaces to this
     * card we need to update the bookkeeping values which indicate
     * the current PCI address space allocations.
     */
    PCI_ADDR_SPACE_LIMITS_STORE();
    return(0);
}

/*
 * pcibr_slot_device_init
 * 	Setup the device register in the bridge for this PCI slot.
 */
int
pcibr_slot_device_init(devfs_handle_t pcibr_vhdl,
		       pciio_slot_t slot)
{
    pcibr_soft_t	 pcibr_soft;
    bridge_t		*bridge;
    bridgereg_t		 devreg;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    bridge = pcibr_soft->bs_base;

    /*
     * Adjustments to Device(x)
     * and init of bss_device shadow
     */
    devreg = bridge->b_device[slot].reg;
    devreg &= ~BRIDGE_DEV_PAGE_CHK_DIS;
    devreg |= BRIDGE_DEV_COH | BRIDGE_DEV_VIRTUAL_EN;
#ifdef LITTLE_ENDIAN
    devreg |= BRIDGE_DEV_DEV_SWAP;
#endif
    pcibr_soft->bs_slot[slot].bss_device = devreg;
    bridge->b_device[slot].reg = devreg;

#if DEBUG && PCI_DEBUG
	printk("pcibr Device(%d): 0x%lx\n", slot, bridge->b_device[slot].reg);
#endif

#if DEBUG && PCI_DEBUG
    printk("pcibr: PCI space allocation done.\n");
#endif

    return(0);
}

/*
 * pcibr_slot_guest_info_init
 *	Setup the host/guest relations for a PCI slot.
 */
int
pcibr_slot_guest_info_init(devfs_handle_t pcibr_vhdl,
			   pciio_slot_t	slot)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    pcibr_info_t	pcibr_info;
    pcibr_soft_slot_t	slotp;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    slotp = &pcibr_soft->bs_slot[slot];

    /* create info and verticies for guest slots;
     * for compatibilitiy macros, create info
     * for even unpopulated slots (but do not
     * build verticies for them).
     */
    if (pcibr_soft->bs_slot[slot].bss_ninfo < 1) {
	NEWA(pcibr_infoh, 1);
	pcibr_soft->bs_slot[slot].bss_ninfo = 1;
	pcibr_soft->bs_slot[slot].bss_infos = pcibr_infoh;

	pcibr_info = pcibr_device_info_new
	    (pcibr_soft, slot, PCIIO_FUNC_NONE,
	     PCIIO_VENDOR_ID_NONE, PCIIO_DEVICE_ID_NONE);

	if (pcibr_soft->bs_slot[slot].has_host) {
	    slotp->slot_conn = pciio_device_info_register
		(pcibr_vhdl, &pcibr_info->f_c);
	}
    }

    /* generate host/guest relations
     */
    if (pcibr_soft->bs_slot[slot].has_host) {
	int  host = pcibr_soft->bs_slot[slot].host_slot;
	pcibr_soft_slot_t host_slotp = &pcibr_soft->bs_slot[host];

	hwgraph_edge_add(slotp->slot_conn,
			 host_slotp->slot_conn,
			 EDGE_LBL_HOST);

	/* XXX- only gives us one guest edge per
	 * host. If/when we have a host with more than
	 * one guest, we will need to figure out how
	 * the host finds all its guests, and sorts
	 * out which one is which.
	 */
	hwgraph_edge_add(host_slotp->slot_conn,
			 slotp->slot_conn,
			 EDGE_LBL_GUEST);
    }

    return(0);
}


/*
 * pcibr_slot_call_device_attach
 *	This calls the associated driver attach routine for the PCI
 * 	card in this slot.
 */
int
pcibr_slot_call_device_attach(devfs_handle_t pcibr_vhdl,
			      pciio_slot_t slot,
			      int          drv_flags)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    pcibr_info_t	pcibr_info;
    async_attach_t	aa = NULL;
    int			func;
    devfs_handle_t	xconn_vhdl,conn_vhdl;
    int			nfunc;
    int                 error_func;
    int                 error_slot = 0;
    int                 error = ENODEV;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);


    if (pcibr_soft->bs_slot[slot].has_host) {
        return(EPERM);
    }
    
    xconn_vhdl = pcibr_soft->bs_conn;
    aa = async_attach_get_info(xconn_vhdl);

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;
    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;

    for (func = 0; func < nfunc; ++func) {

	pcibr_info = pcibr_infoh[func];
	
	if (!pcibr_info)
	    continue;

	if (pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE)
	    continue;

	conn_vhdl = pcibr_info->f_vertex;

#ifdef LATER
	/*
	 * Activate if and when we support cdl.
	 */
	if (aa)
	    async_attach_add_info(conn_vhdl, aa);
#endif	/* LATER */

	error_func = pciio_device_attach(conn_vhdl, drv_flags);

        pcibr_info->f_att_det_error = error_func;

	if (error_func)
	    error_slot = error_func;

        error = error_slot;

    }				/* next func */

    if (error) {
	if ((error != ENODEV) && (error != EUNATCH))
	    pcibr_soft->bs_slot[slot].slot_status |= SLOT_STARTUP_INCMPLT;
    } else {
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_STARTUP_CMPLT;
    }
        
    return(error);
}

/*
 * pcibr_slot_call_device_detach
 *	This calls the associated driver detach routine for the PCI
 * 	card in this slot.
 */
int
pcibr_slot_call_device_detach(devfs_handle_t pcibr_vhdl,
			      pciio_slot_t slot,
			      int          drv_flags)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    pcibr_info_t	pcibr_info;
    int			func;
    devfs_handle_t	conn_vhdl = GRAPH_VERTEX_NONE;
    int			nfunc;
    int                 error_func;
    int                 error_slot = 0;
    int                 error = ENODEV;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    if (pcibr_soft->bs_slot[slot].has_host)
        return(EPERM);

    /* Make sure that we do not detach a system critical function vertex */
    if(pcibr_is_slot_sys_critical(pcibr_vhdl, slot))
        return(EPERM);

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;
    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;

    for (func = 0; func < nfunc; ++func) {

	pcibr_info = pcibr_infoh[func];
	
	if (!pcibr_info)
	    continue;

	if (pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE)
	    continue;

	conn_vhdl = pcibr_info->f_vertex;

	error_func = pciio_device_detach(conn_vhdl, drv_flags);

        pcibr_info->f_att_det_error = error_func;

	if (error_func)
	    error_slot = error_func;

	error = error_slot;

    }				/* next func */

    pcibr_soft->bs_slot[slot].slot_status &= ~SLOT_STATUS_MASK;

    if (error) {
	if ((error != ENODEV) && (error != EUNATCH))
            pcibr_soft->bs_slot[slot].slot_status |= SLOT_SHUTDOWN_INCMPLT;
    } else {
        if (conn_vhdl != GRAPH_VERTEX_NONE) 
            pcibr_device_unregister(conn_vhdl);
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_SHUTDOWN_CMPLT;
    }
        
    return(error);
}

#ifdef LATER

/*
 * pcibr_slot_attach
 *	This is a place holder routine to keep track of all the
 *	slot-specific initialization that needs to be done.
 *	This is usually called when we want to initialize a new
 * 	PCI card on the bus.
 */
int
pcibr_slot_attach(devfs_handle_t pcibr_vhdl,
		  pciio_slot_t slot,
		  int          drv_flags,
		  char        *l1_msg,
                  int         *sub_errorp)
{
    pcibr_soft_t  pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    timespec_t    ts;
    int		  error;

    if (!(pcibr_soft->bs_slot[slot].slot_status & SLOT_POWER_UP)) { 
        /* Power-up the slot */
        error = pcibr_slot_pwr(pcibr_vhdl, slot, L1_REQ_PCI_UP, l1_msg);
        if (error) {
            if (sub_errorp)
                *sub_errorp = error;
            return(PCI_L1_ERR);
        } else {
            pcibr_soft->bs_slot[slot].slot_status &= ~SLOT_POWER_MASK;
            pcibr_soft->bs_slot[slot].slot_status |= SLOT_POWER_UP;
        }

#ifdef LATER
        /*
         * Allow cards like the Alteon Gigabit Ethernet Adapter to complete
         * on-card initialization following the slot reset
         */
         ts.tv_sec = 0;                      /* 0 secs */
         ts.tv_nsec = 500 * (1000 * 1000);   /* 500 msecs */
         nano_delay(&ts);
#else
#endif
#if 0
        /* Reset the slot */
        error = pcibr_slot_reset(pcibr_vhdl, slot)
        if (error) {
            if (sub_errorp)
                *sub_errorp = error;
            return(PCI_SLOT_RESET_ERR);
        }
#endif

        /* Find out what is out there */
        error = pcibr_slot_info_init(pcibr_vhdl, slot);
        if (error) {
            if (sub_errorp)
                *sub_errorp = error;
            return(PCI_SLOT_INFO_INIT_ERR);
        }

        /* Set up the address space for this slot in the PCI land */
        error = pcibr_slot_addr_space_init(pcibr_vhdl, slot);
        if (error) {
            if (sub_errorp)
                *sub_errorp = error;
            return(PCI_SLOT_ADDR_INIT_ERR);
        }

        /* Setup the device register */
        error = pcibr_slot_device_init(pcibr_vhdl, slot);
        if (error) {
            if (sub_errorp)
                *sub_errorp = error;
            return(PCI_SLOT_DEV_INIT_ERR);
        }

        /* Setup host/guest relations */
        error = pcibr_slot_guest_info_init(pcibr_vhdl, slot);
        if (error) {
            if (sub_errorp)
                *sub_errorp = error;
            return(PCI_SLOT_GUEST_INIT_ERR);
        }

        /* Initial RRB management */
        error = pcibr_slot_initial_rrb_alloc(pcibr_vhdl, slot);
        if (error) {
            if (sub_errorp)
                *sub_errorp = error;
            return(PCI_SLOT_RRB_ALLOC_ERR);
        }

    }

    /* Call the device attach */
    error = pcibr_slot_call_device_attach(pcibr_vhdl, slot, drv_flags);
    if (error) {
        if (sub_errorp)
            *sub_errorp = error;
        if (error == EUNATCH)
            return(PCI_NO_DRIVER);
        else
            return(PCI_SLOT_DRV_ATTACH_ERR);
    }

    return(0);
}
#endif	/* LATER */

/*
 * pcibr_slot_detach
 *	This is a place holder routine to keep track of all the
 *	slot-specific freeing that needs to be done.
 */
int
pcibr_slot_detach(devfs_handle_t pcibr_vhdl,
		  pciio_slot_t slot,
		  int          drv_flags)
{
    int		  error;
    
    /* Call the device detach function */
    error = (pcibr_slot_call_device_detach(pcibr_vhdl, slot, drv_flags));
    return (error);

}

/*
 * pcibr_is_slot_sys_critical
 *      Check slot for any functions that are system critical.
 *      Return 1 if any are system critical or 0 otherwise.
 *
 *      This function will always return 0 when called by 
 *      pcibr_attach() because the system critical vertices 
 *      have not yet been set in the hwgraph.
 */
int
pcibr_is_slot_sys_critical(devfs_handle_t pcibr_vhdl,
                      pciio_slot_t slot)
{
    pcibr_soft_t        pcibr_soft;
    pcibr_info_h        pcibr_infoh;
    pcibr_info_t        pcibr_info;
    devfs_handle_t        conn_vhdl = GRAPH_VERTEX_NONE;
    int                 nfunc;
    int                 func;
    boolean_t		is_sys_critical_vertex(devfs_handle_t);

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
        return(0);

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;
    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;

    for (func = 0; func < nfunc; ++func) {

        pcibr_info = pcibr_infoh[func];
        if (!pcibr_info)
            continue;

        if (pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE)
            continue;

        conn_vhdl = pcibr_info->f_vertex;
        if (is_sys_critical_vertex(conn_vhdl)) { 
#if defined(SUPPORT_PRINTING_V_FORMAT)
            printk(KERN_WARNING  "%v is a system critical device vertex\n", conn_vhdl);
#else
            printk(KERN_WARNING  "%p is a system critical device vertex\n", (void *)conn_vhdl);
#endif
            return(1); 
        }

    }

    return(0);
}

/*
 * pcibr_probe_slot: read a config space word
 * while trapping any errors; reutrn zero if
 * all went OK, or nonzero if there was an error.
 * The value read, if any, is passed back
 * through the valp parameter.
 */
int
pcibr_probe_slot(bridge_t *bridge,
		 cfg_p cfg,
		 unsigned *valp)
{
    int                     rv;
    bridgereg_t             old_enable, new_enable;
    int badaddr_val(volatile void *, int, volatile void *);

    old_enable = bridge->b_int_enable;
    new_enable = old_enable & ~BRIDGE_IMR_PCI_MST_TIMEOUT;

    bridge->b_int_enable = new_enable;

	/*
	 * The xbridge doesn't clear b_err_int_view unless
	 * multi-err is cleared...
	 */
	if (is_xbridge(bridge))
	    if (bridge->b_err_int_view & BRIDGE_ISR_PCI_MST_TIMEOUT) {
		bridge->b_int_rst_stat = BRIDGE_IRR_MULTI_CLR;
	    }

    if (bridge->b_int_status & BRIDGE_IRR_PCI_GRP) {
	bridge->b_int_rst_stat = BRIDGE_IRR_PCI_GRP_CLR;
	(void) bridge->b_wid_tflush;	/* flushbus */
    }
    rv = badaddr_val((void *) cfg, 4, valp);

	/*
	 * The xbridge doesn't set master timeout in b_int_status
	 * here.  Fortunately it's in error_interrupt_view.
	 */
	if (is_xbridge(bridge))
	    if (bridge->b_err_int_view & BRIDGE_ISR_PCI_MST_TIMEOUT) {
		bridge->b_int_rst_stat = BRIDGE_IRR_MULTI_CLR;
		rv = 1;		/* unoccupied slot */
	    }

    bridge->b_int_enable = old_enable;
    bridge->b_wid_tflush;		/* wait until Bridge PIO complete */

    return rv;
}

void
pcibr_device_info_free(devfs_handle_t pcibr_vhdl, pciio_slot_t slot)
{
    pcibr_soft_t	pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    pcibr_info_t	pcibr_info;
    pciio_function_t	func;
    pcibr_soft_slot_t	slotp = &pcibr_soft->bs_slot[slot];
    int			nfunc = slotp->bss_ninfo;
    int                 bar;
    int                 devio_index;
    int                 s;


    for (func = 0; func < nfunc; func++) {
	pcibr_info = slotp->bss_infos[func];

	if (!pcibr_info) 
	    continue;

        s = pcibr_lock(pcibr_soft);

        for (bar = 0; bar < PCI_CFG_BASE_ADDRS; bar++) {
            if (pcibr_info->f_window[bar].w_space == PCIIO_SPACE_NONE)
                continue;

            /* Get index of the DevIO(x) register used to access this BAR */
            devio_index = pcibr_info->f_window[bar].w_devio_index;

 
            /* On last use, clear the DevIO(x) used to access this BAR */
            if (! --pcibr_soft->bs_slot[devio_index].bss_devio.bssd_ref_cnt) {
               pcibr_soft->bs_slot[devio_index].bss_devio.bssd_space =
                                                       PCIIO_SPACE_NONE; 
               pcibr_soft->bs_slot[devio_index].bss_devio.bssd_base =
                                                       PCIBR_D32_BASE_UNSET;
               pcibr_soft->bs_slot[devio_index].bss_device = 0;
            }
        }

        pcibr_unlock(pcibr_soft, s);

	slotp->bss_infos[func] = 0;
	pciio_device_info_unregister(pcibr_vhdl, &pcibr_info->f_c);
	pciio_device_info_free(&pcibr_info->f_c);

	DEL(pcibr_info);
    }

    /* Reset the mapping usage counters */
    slotp->bss_pmu_uctr = 0;
    slotp->bss_d32_uctr = 0;
    slotp->bss_d64_uctr = 0;

    /* Clear the Direct translation info */
    slotp->bss_d64_base = PCIBR_D64_BASE_UNSET;
    slotp->bss_d64_flags = 0;
    slotp->bss_d32_base = PCIBR_D32_BASE_UNSET;
    slotp->bss_d32_flags = 0;

    /* Clear out shadow info necessary for the external SSRAM workaround */
    slotp->bss_ext_ates_active = ATOMIC_INIT(0);
    slotp->bss_cmd_pointer = 0;
    slotp->bss_cmd_shadow = 0;

}
