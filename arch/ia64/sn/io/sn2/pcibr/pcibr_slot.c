/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/uaccess.h>
#include <asm/sn/iograph.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/sn_sal.h>

extern pcibr_info_t     pcibr_info_get(vertex_hdl_t);
extern int              pcibr_widget_to_bus(vertex_hdl_t pcibr_vhdl);
extern pcibr_info_t     pcibr_device_info_new(pcibr_soft_t, pciio_slot_t, pciio_function_t, pciio_vendor_id_t, pciio_device_id_t);
extern int		pcibr_slot_initial_rrb_alloc(vertex_hdl_t,pciio_slot_t);
extern int		pcibr_pcix_rbars_calc(pcibr_soft_t);

extern char *pci_space[];

int pcibr_slot_info_init(vertex_hdl_t pcibr_vhdl, pciio_slot_t slot);
int pcibr_slot_info_free(vertex_hdl_t pcibr_vhdl, pciio_slot_t slot);
int pcibr_slot_addr_space_init(vertex_hdl_t pcibr_vhdl,  pciio_slot_t slot);
int pcibr_slot_pcix_rbar_init(pcibr_soft_t pcibr_soft,  pciio_slot_t slot);
int pcibr_slot_device_init(vertex_hdl_t pcibr_vhdl,  pciio_slot_t slot);
int pcibr_slot_guest_info_init(vertex_hdl_t pcibr_vhdl,  pciio_slot_t slot);
int pcibr_slot_call_device_attach(vertex_hdl_t pcibr_vhdl,
		 pciio_slot_t slot, int drv_flags);
int pcibr_slot_call_device_detach(vertex_hdl_t pcibr_vhdl,
		 pciio_slot_t slot, int drv_flags);
int pcibr_slot_detach(vertex_hdl_t pcibr_vhdl, pciio_slot_t slot,
                 int drv_flags, char *l1_msg, int *sub_errorp);
static int pcibr_probe_slot(pcibr_soft_t, cfg_p, unsigned int *);
static int pcibr_probe_work(pcibr_soft_t pcibr_soft, void *addr, int len, void *valp);
void pcibr_device_info_free(vertex_hdl_t, pciio_slot_t);
iopaddr_t pcibr_bus_addr_alloc(pcibr_soft_t, pciio_win_info_t, 
                               pciio_space_t, int, int, int);
void pcibr_bus_addr_free(pciio_win_info_t);
cfg_p pcibr_find_capability(cfg_p, unsigned);
extern uint64_t  do_pcibr_config_get(cfg_p, unsigned, unsigned);
void do_pcibr_config_set(cfg_p, unsigned, unsigned, uint64_t); 
int pcibr_slot_pwr(vertex_hdl_t pcibr_vhdl, pciio_slot_t slot, int up, char *err_msg);


/* 
 * PCI-X Max Outstanding Split Transactions translation array and Max Memory
 * Read Byte Count translation array, as defined in the PCI-X Specification.
 * Section 7.2.3 & 7.2.4 of PCI-X Specification - rev 1.0
 */
#define MAX_SPLIT_TABLE 8
#define MAX_READCNT_TABLE 4
int max_splittrans_to_numbuf[MAX_SPLIT_TABLE] = {1, 2, 3, 4, 8, 12, 16, 32};
int max_readcount_to_bufsize[MAX_READCNT_TABLE] = {512, 1024, 2048, 4096 };

#ifdef CONFIG_HOTPLUG_PCI_SGI

/*
 * PCI slot manipulation errors from the system controller, and their
 * associated descriptions
 */
#define SYSCTL_REQERR_BASE	(-106000)
#define SYSCTL_PCI_ERROR_BASE	(SYSCTL_REQERR_BASE - 100)
#define SYSCTL_PCIX_ERROR_BASE	(SYSCTL_REQERR_BASE - 3000)

struct sysctl_pci_error_s {

    int	 	error;
    char	*msg;

} sysctl_pci_errors[] = {

#define SYSCTL_PCI_UNINITIALIZED	(SYSCTL_PCI_ERROR_BASE - 0)
    { SYSCTL_PCI_UNINITIALIZED, "module not initialized" },

#define SYSCTL_PCI_UNSUPPORTED_BUS	(SYSCTL_PCI_ERROR_BASE - 1)
    { SYSCTL_PCI_UNSUPPORTED_BUS, "unsupported bus" },

#define SYSCTL_PCI_UNSUPPORTED_SLOT	(SYSCTL_PCI_ERROR_BASE - 2)
    { SYSCTL_PCI_UNSUPPORTED_SLOT, "unsupported slot" },

#define SYSCTL_PCI_POWER_NOT_OKAY	(SYSCTL_PCI_ERROR_BASE - 3)
    { SYSCTL_PCI_POWER_NOT_OKAY, "slot power not okay" },

#define SYSCTL_PCI_CARD_NOT_PRESENT	(SYSCTL_PCI_ERROR_BASE - 4)
    { SYSCTL_PCI_CARD_NOT_PRESENT, "card not present" },

#define SYSCTL_PCI_POWER_LIMIT		(SYSCTL_PCI_ERROR_BASE - 5)
    { SYSCTL_PCI_POWER_LIMIT, "power limit reached - some cards not powered up" },

#define SYSCTL_PCI_33MHZ_ON_66MHZ	(SYSCTL_PCI_ERROR_BASE - 6)
    { SYSCTL_PCI_33MHZ_ON_66MHZ, "cannot add a 33 MHz card to an active 66 MHz bus" },

#define SYSCTL_PCI_INVALID_ORDER	(SYSCTL_PCI_ERROR_BASE - 7)
    { SYSCTL_PCI_INVALID_ORDER, "invalid reset order" },

#define SYSCTL_PCI_DOWN_33MHZ		(SYSCTL_PCI_ERROR_BASE - 8)
    { SYSCTL_PCI_DOWN_33MHZ, "cannot power down a 33 MHz card on an active bus" },

#define SYSCTL_PCI_RESET_33MHZ		(SYSCTL_PCI_ERROR_BASE - 9)
    { SYSCTL_PCI_RESET_33MHZ, "cannot reset a 33 MHz card on an active bus" },

#define SYSCTL_PCI_SLOT_NOT_UP		(SYSCTL_PCI_ERROR_BASE - 10)
    { SYSCTL_PCI_SLOT_NOT_UP, "cannot reset a slot that is not powered up" },

#define SYSCTL_PCIX_UNINITIALIZED	(SYSCTL_PCIX_ERROR_BASE - 0)
    { SYSCTL_PCIX_UNINITIALIZED, "module not initialized" },

#define SYSCTL_PCIX_UNSUPPORTED_BUS	(SYSCTL_PCIX_ERROR_BASE - 1)
    { SYSCTL_PCIX_UNSUPPORTED_BUS, "unsupported bus" },

#define SYSCTL_PCIX_UNSUPPORTED_SLOT	(SYSCTL_PCIX_ERROR_BASE - 2)
    { SYSCTL_PCIX_UNSUPPORTED_SLOT, "unsupported slot" },

#define SYSCTL_PCIX_POWER_NOT_OKAY	(SYSCTL_PCIX_ERROR_BASE - 3)
    { SYSCTL_PCIX_POWER_NOT_OKAY, "slot power not okay" },

#define SYSCTL_PCIX_CARD_NOT_PRESENT	(SYSCTL_PCIX_ERROR_BASE - 4)
    { SYSCTL_PCIX_CARD_NOT_PRESENT, "card not present" },

#define SYSCTL_PCIX_POWER_LIMIT		(SYSCTL_PCIX_ERROR_BASE - 5)
    { SYSCTL_PCIX_POWER_LIMIT, "power limit reached - some cards not powered up" },

#define SYSCTL_PCIX_33MHZ_ON_66MHZ	(SYSCTL_PCIX_ERROR_BASE - 6)
    { SYSCTL_PCIX_33MHZ_ON_66MHZ, "cannot add a 33 MHz card to an active 66 MHz bus" },

#define SYSCTL_PCIX_PCI_ON_PCIX		(SYSCTL_PCIX_ERROR_BASE - 7)
    { SYSCTL_PCIX_PCI_ON_PCIX, "cannot add a PCI card to an active PCIX bus" },

#define SYSCTL_PCIX_ANYTHING_ON_133MHZ		(SYSCTL_PCIX_ERROR_BASE - 8)
    { SYSCTL_PCIX_ANYTHING_ON_133MHZ, "cannot add any card to an active 133MHz PCIX bus" },

#define SYSCTL_PCIX_X66MHZ_ON_X100MHZ		(SYSCTL_PCIX_ERROR_BASE - 9)
    { SYSCTL_PCIX_X66MHZ_ON_X100MHZ, "cannot add a PCIX 66MHz card to an active 100MHz PCIX bus" },

#define SYSCTL_PCIX_INVALID_ORDER	(SYSCTL_PCIX_ERROR_BASE - 10)
    { SYSCTL_PCIX_INVALID_ORDER, "invalid reset order" },

#define SYSCTL_PCIX_DOWN_33MHZ		(SYSCTL_PCIX_ERROR_BASE - 11)
    { SYSCTL_PCIX_DOWN_33MHZ, "cannot power down a 33 MHz card on an active bus" },

#define SYSCTL_PCIX_RESET_33MHZ		(SYSCTL_PCIX_ERROR_BASE - 12)
    { SYSCTL_PCIX_RESET_33MHZ, "cannot reset a 33 MHz card on an active bus" },

#define SYSCTL_PCIX_SLOT_NOT_UP		(SYSCTL_PCIX_ERROR_BASE - 13)
    { SYSCTL_PCIX_SLOT_NOT_UP, "cannot reset a slot that is not powered up" },

#define SYSCTL_PCIX_INVALID_BUS_SETTING	(SYSCTL_PCIX_ERROR_BASE - 14)
    { SYSCTL_PCIX_INVALID_BUS_SETTING, "invalid bus type/speed selection (PCIX<66MHz, PCI>66MHz)" },

#define SYSCTL_PCIX_INVALID_DEPENDENT_SLOT (SYSCTL_PCIX_ERROR_BASE - 15)
    { SYSCTL_PCIX_INVALID_DEPENDENT_SLOT, "invalid dependent slot in PCI slot configuration" },

#define SYSCTL_PCIX_SHARED_IDSELECT	(SYSCTL_PCIX_ERROR_BASE - 16)
    { SYSCTL_PCIX_SHARED_IDSELECT, "cannot enable two slots sharing the same IDSELECT" },

#define SYSCTL_PCIX_SLOT_DISABLED	(SYSCTL_PCIX_ERROR_BASE - 17)
    { SYSCTL_PCIX_SLOT_DISABLED, "slot is disabled" },

}; /* end sysctl_pci_errors[] */

/*
 * look up an error message for PCI operations that fail
 */
static void
sysctl_pci_error_lookup(int error, char *err_msg)
{
    int i;
    struct sysctl_pci_error_s *e = sysctl_pci_errors;
    
    for (i = 0; 
	 i < (sizeof(sysctl_pci_errors) / sizeof(*e));
	 i++, e++ )
    {
	if (e->error == error)
	{
	    strcpy(err_msg, e->msg);
	    return;
	}
    }

    sprintf(err_msg, "unrecognized PCI error type");
}

/*
 * pcibr_slot_attach
 *	This is a place holder routine to keep track of all the
 *	slot-specific initialization that needs to be done.
 *	This is usually called when we want to initialize a new
 * 	PCI card on the bus.
 */
int
pcibr_slot_attach(vertex_hdl_t pcibr_vhdl,
		  pciio_slot_t slot,
		  int          drv_flags,
		  char        *l1_msg,
                  int         *sub_errorp)
{
    pcibr_soft_t  pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    int		  error;

    if (!(pcibr_soft->bs_slot[slot].slot_status & PCI_SLOT_POWER_ON)) {
	uint64_t speed;
	uint64_t mode;

        /* Power-up the slot */
        error = pcibr_slot_pwr(pcibr_vhdl, slot, PCI_REQ_SLOT_POWER_ON, l1_msg);

        if (error) {
            if (sub_errorp)
                *sub_errorp = error;
            return(PCI_L1_ERR);
        } else {
            pcibr_soft->bs_slot[slot].slot_status &= ~PCI_SLOT_POWER_MASK;
            pcibr_soft->bs_slot[slot].slot_status |= PCI_SLOT_POWER_ON;
        }

	/* The speed/mode of the bus may have changed due to the hotplug */
	speed = pcireg_speed_get(pcibr_soft);
	mode = pcireg_mode_get(pcibr_soft);
	pcibr_soft->bs_bridge_mode = ((speed << 1) | mode);

        /*
         * Allow cards like the Alteon Gigabit Ethernet Adapter to complete
         * on-card initialization following the slot reset
         */
        set_current_state (TASK_INTERRUPTIBLE);
        schedule_timeout (HZ);

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

	/* Allocate the PCI-X Read Buffer Attribute Registers (RBARs)*/
	if (IS_PCIX(pcibr_soft)) {
	    int tmp_slot;

	    /* Recalculate the RBARs for all the devices on the bus.  Only
	     * return an error if we error for the given 'slot'
	     */
	    pcibr_soft->bs_pcix_rbar_inuse = 0;
	    pcibr_soft->bs_pcix_rbar_avail = NUM_RBAR;
	    pcibr_soft->bs_pcix_rbar_percent_allowed = 
					pcibr_pcix_rbars_calc(pcibr_soft);
	    for (tmp_slot = pcibr_soft->bs_min_slot;
			tmp_slot < PCIBR_NUM_SLOTS(pcibr_soft); ++tmp_slot) {
		if (tmp_slot == slot)
		    continue;	/* skip this 'slot', we do it below */
                (void)pcibr_slot_pcix_rbar_init(pcibr_soft, tmp_slot);
	    }

	    error = pcibr_slot_pcix_rbar_init(pcibr_soft, slot);
	    if (error) {
		if (sub_errorp)
		    *sub_errorp = error;
		return(PCI_SLOT_RBAR_ALLOC_ERR);
	    }
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

/*
 * pcibr_slot_enable
 *	Enable the PCI slot for a hot-plug insert.
 */
int
pcibr_slot_enable(vertex_hdl_t pcibr_vhdl, struct pcibr_slot_enable_req_s *req_p)
{
    pcibr_soft_t                   pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    pciio_slot_t                   slot = req_p->req_device;
    int                            error = 0;

    /* Make sure that we are dealing with a bridge device vertex */
    if (!pcibr_soft) {
        return(PCI_NOT_A_BRIDGE);
    }

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_HOTPLUG, pcibr_vhdl,
                "pcibr_slot_enable: pcibr_soft=0x%lx, slot=%d, req_p=0x%lx\n",
                pcibr_soft, slot, req_p));

    /* Check for the valid slot */
    if (!PCIBR_VALID_SLOT(pcibr_soft, slot))
        return(PCI_NOT_A_SLOT);

    if (pcibr_soft->bs_slot[slot].slot_status & PCI_SLOT_ENABLE_CMPLT) {
        error = PCI_SLOT_ALREADY_UP;
        goto enable_unlock;
    }

    error = pcibr_slot_attach(pcibr_vhdl, slot, 0,
                              req_p->req_resp.resp_l1_msg,
			      &req_p->req_resp.resp_sub_errno);

    req_p->req_resp.resp_l1_msg[PCI_L1_QSIZE] = '\0';

    enable_unlock:

    return(error);
}

/*
 * pcibr_slot_disable
 *	Disable the PCI slot for a hot-plug removal.
 */
int
pcibr_slot_disable(vertex_hdl_t pcibr_vhdl, struct pcibr_slot_disable_req_s *req_p)
{
    pcibr_soft_t                   pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    pciio_slot_t                   slot = req_p->req_device;
    int                            error = 0;
    pciio_slot_t                   tmp_slot;

    /* Make sure that we are dealing with a bridge device vertex */
    if (!pcibr_soft) {
        return(PCI_NOT_A_BRIDGE);
    }

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_HOTPLUG, pcibr_vhdl,
                "pcibr_slot_disable: pcibr_soft=0x%lx, slot=%d, req_p=0x%lx\n",
                pcibr_soft, slot, req_p));

    /* Check for valid slot */
    if (!PCIBR_VALID_SLOT(pcibr_soft, slot))
        return(PCI_NOT_A_SLOT);

    if ((pcibr_soft->bs_slot[slot].slot_status & PCI_SLOT_DISABLE_CMPLT) ||
       ((pcibr_soft->bs_slot[slot].slot_status & PCI_SLOT_STATUS_MASK) == 0)) {
        error = PCI_SLOT_ALREADY_DOWN;
        /*
         * RJR - Should we invoke an L1 slot power-down command just in case
         *       a previous shut-down failed to power-down the slot?
         */
        goto disable_unlock;
    }

    /* Do not allow the last 33 MHz card to be removed */
    if (IS_33MHZ(pcibr_soft)) {
        for (tmp_slot = pcibr_soft->bs_first_slot;
             tmp_slot <= pcibr_soft->bs_last_slot; tmp_slot++)
            if (tmp_slot != slot)
                if (pcibr_soft->bs_slot[tmp_slot].slot_status & PCI_SLOT_POWER_ON) {
                    error++;
                    break;
                }
        if (!error) {
            error = PCI_EMPTY_33MHZ;
            goto disable_unlock;
        }
    }

    if (req_p->req_action == PCI_REQ_SLOT_ELIGIBLE)
	return(0);

    error = pcibr_slot_detach(pcibr_vhdl, slot, 1,
                              req_p->req_resp.resp_l1_msg,
			      &req_p->req_resp.resp_sub_errno);

    req_p->req_resp.resp_l1_msg[PCI_L1_QSIZE] = '\0';

    disable_unlock:

    return(error);
}

/*
 * pcibr_slot_pwr
 *      Power-up or power-down a PCI slot.  This routines makes calls to
 *      the L1 system controller driver which requires "external" slot#.
 */
int
pcibr_slot_pwr(vertex_hdl_t pcibr_vhdl,
               pciio_slot_t slot,
               int          up,
	       char        *err_msg)
{
    pcibr_soft_t        pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    nasid_t             nasid;
    u64			connection_type;
    int			rv;

    nasid = NASID_GET(pcibr_soft->bs_base);
    connection_type = SAL_SYSCTL_IO_XTALK;

    rv = (int) ia64_sn_sysctl_iobrick_pci_op
	(nasid,
	 connection_type,
	 (u64) pcibr_widget_to_bus(pcibr_vhdl),
	 PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot),
	 (up ? SAL_SYSCTL_PCI_POWER_UP : SAL_SYSCTL_PCI_POWER_DOWN));

    if (!rv) {
	/* everything's okay; no error message */
	*err_msg = '\0';
    }
    else {
	/* there was a problem; look up an appropriate error message */
	sysctl_pci_error_lookup(rv, err_msg);
    }
    return rv;
}

#endif /* CONFIG_HOTPLUG_PCI_SGI */

/*
 * pcibr_slot_info_init
 *	Probe for this slot and see if it is populated.
 *	If it is populated initialize the generic PCI infrastructural
 * 	information associated with this particular PCI device.
 */
int
pcibr_slot_info_init(vertex_hdl_t 	pcibr_vhdl,
		     pciio_slot_t 	slot)
{
    pcibr_soft_t	    pcibr_soft;
    pcibr_info_h	    pcibr_infoh;
    pcibr_info_t	    pcibr_info;
    cfg_p                   cfgw;
    unsigned                idword;
    unsigned                pfail;
    unsigned                idwords[8];
    pciio_vendor_id_t       vendor;
    pciio_device_id_t       device;
    unsigned                htype;
    unsigned                lt_time;
    int                     nbars;
    cfg_p                   wptr;
    cfg_p                   pcix_cap;
    int                     win;
    pciio_space_t           space;
    int			    nfunc;
    pciio_function_t	    rfunc;
    int			    func;
    vertex_hdl_t	    conn_vhdl;
    pcibr_soft_slot_t	    slotp;
    uint64_t		    device_reg;

    /* Get the basic software information required to proceed */
    pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    if (!pcibr_soft)
	return -EINVAL;

    if (!PCIBR_VALID_SLOT(pcibr_soft, slot))
	return -EINVAL;

    /* If we have a host slot (eg:- IOC3 has 2 PCI slots and the initialization
     * is done by the host slot then we are done.
     */
    if (pcibr_soft->bs_slot[slot].has_host) {
	return 0;
    }

    /* Try to read the device-id/vendor-id from the config space */
    cfgw = pcibr_slot_config_addr(pcibr_soft, slot, 0);

    if (pcibr_probe_slot(pcibr_soft, cfgw, &idword)) 
	return -ENODEV;

    slotp = &pcibr_soft->bs_slot[slot];
#ifdef CONFIG_HOTPLUG_PCI_SGI
    slotp->slot_status |= SLOT_POWER_UP;
#endif

    vendor = 0xFFFF & idword;
    device = 0xFFFF & (idword >> 16);

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_PROBE, pcibr_vhdl,
		"pcibr_slot_info_init: slot=%d, vendor=0x%x, device=0x%x\n",
		PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot), vendor, device));

    /* If the vendor id is not valid then the slot is not populated
     * and we are done.
     */
    if (vendor == 0xFFFF) 
	return -ENODEV;
    
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
	    cfgw = pcibr_func_config_addr(pcibr_soft, 0, slot, func, 0);
	    if (pcibr_probe_slot(pcibr_soft, cfgw, &idwords[func])) {
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
        cfgw = pcibr_slot_config_addr(pcibr_soft, slot, 0);
    }
    pcibr_infoh = kmalloc(nfunc*sizeof (*(pcibr_infoh)), GFP_KERNEL);
    if ( !pcibr_infoh ) {
	return -ENOMEM;
    }
    memset(pcibr_infoh, 0, nfunc*sizeof (*(pcibr_infoh)));
    
    pcibr_soft->bs_slot[slot].bss_ninfo = nfunc;
    pcibr_soft->bs_slot[slot].bss_infos = pcibr_infoh;

    for (func = 0; func < nfunc; ++func) {
	unsigned                cmd_reg;
	
	if (func) {
	    if (pfail & (1 << func))
		continue;
	    
	    idword = idwords[func];
	    cfgw = pcibr_func_config_addr(pcibr_soft, 0, slot, func, 0);
	    
	    device = 0xFFFF & (idword >> 16);
	    htype = do_pcibr_config_get(cfgw, PCI_CFG_HEADER_TYPE, 1);
	    rfunc = func;
	}
	htype &= 0x7f;
	if (htype != 0x00) {
	    printk(KERN_WARNING 
		"%s pcibr: pci slot %d func %d has strange header type 0x%x\n",
		    pcibr_soft->bs_name, slot, func, htype);
	    nbars = 2;
	} else {
	    nbars = PCI_CFG_BASE_ADDRS;
	}

	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_CONFIG, pcibr_vhdl,
                "pcibr_slot_info_init: slot=%d, func=%d, cfgw=0x%lx\n",
		PCIBR_DEVICE_TO_SLOT(pcibr_soft,slot), func, cfgw));

	/* 
	 * If the latency timer has already been set, by prom or by the
	 * card itself, use that value.  Otherwise look at the device's
	 * 'min_gnt' and attempt to calculate a latency time. 
	 *
	 * NOTE: For now if the device is on the 'real time' arbitration
	 * ring we don't set the latency timer.  
	 *
	 * WAR: SGI's IOC3 and RAD devices target abort if you write a 
	 * single byte into their config space.  So don't set the Latency
	 * Timer for these devices
	 */

	lt_time = do_pcibr_config_get(cfgw, PCI_CFG_LATENCY_TIMER, 1);
	device_reg = pcireg_device_get(pcibr_soft, slot);
	if ((lt_time == 0) && !(device_reg & BRIDGE_DEV_RT)) {
	     unsigned	min_gnt;
	     unsigned	min_gnt_mult;
	    
	    /* 'min_gnt' indicates how long of a burst period a device
	     * needs in increments of 250ns.  But latency timer is in
	     * PCI clock cycles, so a conversion is needed.
	     */
	    min_gnt = do_pcibr_config_get(cfgw, PCI_MIN_GNT, 1);

	    if (IS_133MHZ(pcibr_soft))
		min_gnt_mult = 32;	/* 250ns @ 133MHz in clocks */
	    else if (IS_100MHZ(pcibr_soft))
		min_gnt_mult = 24;	/* 250ns @ 100MHz in clocks */
	    else if (IS_66MHZ(pcibr_soft))
		min_gnt_mult = 16;	/* 250ns @ 66MHz, in clocks */
	    else
		min_gnt_mult = 8;	/* 250ns @ 33MHz, in clocks */

	    if ((min_gnt != 0) && ((min_gnt * min_gnt_mult) < 256))
		lt_time = (min_gnt * min_gnt_mult);
	    else
		lt_time = 4 * min_gnt_mult;	  /* 1 micro second */

	    do_pcibr_config_set(cfgw, PCI_CFG_LATENCY_TIMER, 1, lt_time);

	    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_CONFIG, pcibr_vhdl,
                    "pcibr_slot_info_init: set Latency Timer for slot=%d, "
		    "func=%d, to 0x%x\n", 
		    PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot), func, lt_time));
	}


	/* In our architecture the setting of the cacheline size isn't 
	 * beneficial for cards in PCI mode, but in PCI-X mode devices
	 * can optionally use the cacheline size value for internal 
	 * device optimizations    (See 7.1.5 of the PCI-X v1.0 spec).
	 * NOTE: cachline size is in doubleword increments
	 */
	if (IS_PCIX(pcibr_soft)) {
	    if (!do_pcibr_config_get(cfgw, PCI_CFG_CACHE_LINE, 1)) {
		do_pcibr_config_set(cfgw, PCI_CFG_CACHE_LINE, 1, 0x20);
		PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_CONFIG, pcibr_vhdl,
			"pcibr_slot_info_init: set CacheLine for slot=%d, "
			"func=%d, to 0x20\n",
			PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot), func));
	    }
	}

	/* Get the PCI-X capability if running in PCI-X mode.  If the func
	 * doesnt have a pcix capability, allocate a PCIIO_VENDOR_ID_NONE
	 * pcibr_info struct so the device driver for that function is not
	 * called.
	 */
	if (IS_PCIX(pcibr_soft)) {
	    if (!(pcix_cap = pcibr_find_capability(cfgw, PCI_CAP_PCIX))) {
		printk(KERN_WARNING
		        "%s: Bus running in PCI-X mode, But card in slot %d, "
		        "func %d not PCI-X capable\n", 
			pcibr_soft->bs_name, slot, func);
		pcibr_device_info_new(pcibr_soft, slot, PCIIO_FUNC_NONE,
		               PCIIO_VENDOR_ID_NONE, PCIIO_DEVICE_ID_NONE);
		continue;
	    }
	    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_CONFIG, pcibr_vhdl,
                    "pcibr_slot_info_init: PCI-X capability at 0x%lx for "
		    "slot=%d, func=%d\n", 
		    pcix_cap, PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot), func));
	} else {
	    pcix_cap = NULL;
	}

	pcibr_info = pcibr_device_info_new
	    (pcibr_soft, slot, rfunc, vendor, device);

	/* Keep a running total of the number of PIC-X functions on the bus
         * and the number of max outstanding split trasnactions that they
	 * have requested.  NOTE: "pcix_cap != NULL" implies IS_PCIX()
	 */
	pcibr_info->f_pcix_cap = (cap_pcix_type0_t *)pcix_cap;
	if (pcibr_info->f_pcix_cap) {
	    int max_out;      /* max outstanding splittrans from status reg */

	    pcibr_soft->bs_pcix_num_funcs++;
	    max_out = pcibr_info->f_pcix_cap->pcix_type0_status.max_out_split;
	    pcibr_soft->bs_pcix_split_tot += max_splittrans_to_numbuf[max_out];
	}

	conn_vhdl = pciio_device_info_register(pcibr_vhdl, &pcibr_info->f_c);
	if (func == 0)
	    slotp->slot_conn = conn_vhdl;

	cmd_reg = do_pcibr_config_get(cfgw, PCI_CFG_COMMAND, 4);
	
	wptr = cfgw + PCI_CFG_BASE_ADDR_0 / 4;

	for (win = 0; win < nbars; ++win) {
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
	    base = do_pcibr_config_get(wptr, (win * 4), 4);

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
			   (do_pcibr_config_get(wptr, ((win + 1)*4), 4) != 0)) {
		    base = 0;	/* outside permissable range */
		}
	    }

	    if (base != 0) {	/* estimate size */
		pciio_space_t	tmp_space = space;
		iopaddr_t	tmp_base;

		size = base & -base;

		/*
		 * Reserve this space in the relavent address map.  Don't
		 * care about the return code from pcibr_bus_addr_alloc().
		 */

		if (space == PCIIO_SPACE_MEM && code != PCI_BA_MEM_1MEG) {
			tmp_space = PCIIO_SPACE_MEM32;
		}

                tmp_base = pcibr_bus_addr_alloc(pcibr_soft,
                                            	&pcibr_info->f_window[win],
                                            	tmp_space,
                                            	base, size, 0);

		PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_BAR, pcibr_vhdl,
			"pcibr_slot_info_init: slot=%d, func=%d win %d "
			"reserving space %s [0x%lx..0x%lx], tmp_base 0x%lx\n",
			PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot), func, win,
			pci_space[tmp_space], (uint64_t)base,
			(uint64_t)(base + size - 1), (uint64_t)tmp_base));
	    } else {		/* calculate size */
		do_pcibr_config_set(wptr, (win * 4), 4, ~0);    /* write 1's */
		size = do_pcibr_config_get(wptr, (win * 4), 4); /* read back */
		size &= mask;	/* keep addr */
		size &= -size;	/* keep lsbit */
		if (size == 0)
		    continue;
	    }	

	    pcibr_info->f_window[win].w_space = space;
	    pcibr_info->f_window[win].w_base = base;
	    pcibr_info->f_window[win].w_size = size;

	    if (code == PCI_BA_MEM_64BIT) {
		win++;		/* skip upper half */
		do_pcibr_config_set(wptr, (win * 4), 4, 0);  /* must be zero */
	    }
	}				/* next win */
    }				/* next func */

    return 0;
}					

/*
 * pcibr_find_capability
 *	Walk the list of capabilities (if it exists) looking for
 *	the requested capability.  Return a cfg_p pointer to the
 *	capability if found, else return NULL
 */
cfg_p
pcibr_find_capability(cfg_p	cfgw,
		      unsigned	capability)
{
    unsigned		cap_nxt;
    unsigned		cap_id;
    int			defend_against_circular_linkedlist = 0;

    /* Check to see if there is a capabilities pointer in the cfg header */
    if (!(do_pcibr_config_get(cfgw, PCI_CFG_STATUS, 2) & PCI_STAT_CAP_LIST)) {
	return NULL;
    }

    /*
     * Read up the capabilities head pointer from the configuration header.
     * Capabilities are stored as a linked list in the lower 48 dwords of
     * config space and are dword aligned. (Note: spec states the least two
     * significant bits of the next pointer must be ignored,  so we mask
     * with 0xfc).
     */
    cap_nxt = (do_pcibr_config_get(cfgw, PCI_CAPABILITIES_PTR, 1) & 0xfc);

    while (cap_nxt && (defend_against_circular_linkedlist <= 48)) {
	cap_id = do_pcibr_config_get(cfgw, cap_nxt, 1);
	if (cap_id == capability) {
	    return (cfg_p)((char *)cfgw + cap_nxt);
	}
	cap_nxt = (do_pcibr_config_get(cfgw, cap_nxt+1, 1) & 0xfc);
	defend_against_circular_linkedlist++;
    }

    return NULL;
}

/*
 * pcibr_slot_info_free
 *	Remove all the PCI infrastructural information associated
 * 	with a particular PCI device.
 */
int
pcibr_slot_info_free(vertex_hdl_t pcibr_vhdl,
                     pciio_slot_t slot)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    int			nfunc;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft)
	return -EINVAL;

    if (!PCIBR_VALID_SLOT(pcibr_soft, slot))
	return -EINVAL;

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;

    pcibr_device_info_free(pcibr_vhdl, slot);

    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;
    kfree(pcibr_infoh);
    pcibr_soft->bs_slot[slot].bss_ninfo = 0;

    return 0;
}

/*
 * pcibr_slot_pcix_rbar_init
 *	Allocate RBARs to the PCI-X functions on a given device
 */
int
pcibr_slot_pcix_rbar_init(pcibr_soft_t pcibr_soft,
			    pciio_slot_t slot)
{
    pcibr_info_h	 pcibr_infoh;
    pcibr_info_t	 pcibr_info;
    int		       	 nfunc;
    int			 func;

    if (!PCIBR_VALID_SLOT(pcibr_soft, slot))
	return -EINVAL;

    if ((nfunc = pcibr_soft->bs_slot[slot].bss_ninfo) < 1)
	return -EINVAL;

    if (!(pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos))
	return -EINVAL;

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_RBAR, pcibr_soft->bs_vhdl,
		"pcibr_slot_pcix_rbar_init for slot %d\n", 
		PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot)));
    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_RBAR, pcibr_soft->bs_vhdl,
		"\tslot/func\trequested\tgiven\tinuse\tavail\n"));

    for (func = 0; func < nfunc; ++func) {
	cap_pcix_type0_t	*pcix_cap_p;
	cap_pcix_stat_reg_t	*pcix_statreg_p;
	cap_pcix_cmd_reg_t	*pcix_cmdreg_p;
	int 			 num_rbar;

	if (!(pcibr_info = pcibr_infoh[func]))
	    continue;

	if (pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE)
	    continue;

	if (!(pcix_cap_p = pcibr_info->f_pcix_cap))
	    continue;

	pcix_statreg_p = &pcix_cap_p->pcix_type0_status;
	pcix_cmdreg_p = &pcix_cap_p->pcix_type0_command;

	/* If there are enough RBARs to satify the number of "max outstanding 
	 * transactions" each function requested (bs_pcix_rbar_percent_allowed
	 * is 100%), then give each function what it requested, otherwise give 
	 * the functions a "percentage of what they requested".
	 */
	if (pcibr_soft->bs_pcix_rbar_percent_allowed >= 100) {
	    pcix_cmdreg_p->max_split = pcix_statreg_p->max_out_split;
	    num_rbar = max_splittrans_to_numbuf[pcix_cmdreg_p->max_split];
	    pcibr_soft->bs_pcix_rbar_inuse += num_rbar;
	    pcibr_soft->bs_pcix_rbar_avail -= num_rbar;
	    pcix_cmdreg_p->max_mem_read_cnt = pcix_statreg_p->max_mem_read_cnt;
	} else {
	    int index;	    /* index into max_splittrans_to_numbuf table */
	    int max_out;    /* max outstanding transactions given to func */

	    /* Calculate the percentage of RBARs this function can have.
	     * NOTE: Every function gets at least 1 RBAR (thus the "+1").
	     * bs_pcix_rbar_percent_allowed is the percentage of what was
	     * requested less this 1 RBAR that all functions automatically 
	     * gets
	     */
	    max_out = ((max_splittrans_to_numbuf[pcix_statreg_p->max_out_split]
			* pcibr_soft->bs_pcix_rbar_percent_allowed) / 100) + 1;

	    /* round down the newly caclulated max_out to a valid number in
	     * max_splittrans_to_numbuf[]
	     */
	    for (index = 0; index < MAX_SPLIT_TABLE-1; index++)
		if (max_splittrans_to_numbuf[index + 1] > max_out)
		    break;

	    pcix_cmdreg_p->max_split = index;
	    num_rbar = max_splittrans_to_numbuf[pcix_cmdreg_p->max_split];
	    pcibr_soft->bs_pcix_rbar_inuse += num_rbar;
            pcibr_soft->bs_pcix_rbar_avail -= num_rbar;
	    pcix_cmdreg_p->max_mem_read_cnt = pcix_statreg_p->max_mem_read_cnt;
	}

	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_RBAR, pcibr_soft->bs_vhdl,
		"\t  %d/%d   \t    %d    \t  %d  \t  %d  \t  %d\n",
		PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot), func,
		max_splittrans_to_numbuf[pcix_statreg_p->max_out_split],
		max_splittrans_to_numbuf[pcix_cmdreg_p->max_split],
		pcibr_soft->bs_pcix_rbar_inuse,
		pcibr_soft->bs_pcix_rbar_avail));
    }
    return 0;
}

int as_debug = 0;
/*
 * pcibr_slot_addr_space_init
 *	Reserve chunks of PCI address space as required by 
 * 	the base registers in the card.
 */
int
pcibr_slot_addr_space_init(vertex_hdl_t pcibr_vhdl,
			   pciio_slot_t	slot)
{
    pcibr_soft_t	 pcibr_soft;
    pcibr_info_h	 pcibr_infoh;
    pcibr_info_t	 pcibr_info;
    iopaddr_t            mask;
    int		       	 nbars;
    int		       	 nfunc;
    int			 func;
    int			 win;
    int                  rc = 0;
    int			 align = 0;
    int			 align_slot;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft)
	return -EINVAL;

    if (!PCIBR_VALID_SLOT(pcibr_soft, slot))
	return -EINVAL;

    /* allocate address space,
     * for windows that have not been
     * previously assigned.
     */
    if (pcibr_soft->bs_slot[slot].has_host) {
	return 0;
    }

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;
    if (nfunc < 1)
	return -EINVAL;

    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;
    if (!pcibr_infoh)
	return -EINVAL;

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
    align_slot = (slot < 2) ? 0x200000 : 0x100000;

    for (func = 0; func < nfunc; ++func) {
	cfg_p                   cfgw;
	cfg_p                   wptr;
	pciio_space_t           space;
	iopaddr_t               base;
	size_t                  size;
	unsigned                pci_cfg_cmd_reg;
	unsigned                pci_cfg_cmd_reg_add = 0;

	pcibr_info = pcibr_infoh[func];

	if (!pcibr_info)
	    continue;

	if (pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE)
	    continue;
	
        cfgw = pcibr_func_config_addr(pcibr_soft, 0, slot, func, 0);
	wptr = cfgw + PCI_CFG_BASE_ADDR_0 / 4;

	if ((do_pcibr_config_get(cfgw, PCI_CFG_HEADER_TYPE, 1) & 0x7f) != 0)
	    nbars = 2;
	else
	    nbars = PCI_CFG_BASE_ADDRS;

	for (win = 0; win < nbars; ++win) {
	    space = pcibr_info->f_window[win].w_space;
	    base = pcibr_info->f_window[win].w_base;
	    size = pcibr_info->f_window[win].w_size;
	    
	    if (size < 1)
		continue;

	    if (base >= size) {
		PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_BAR, pcibr_vhdl,
			"pcibr_slot_addr_space_init: slot=%d, "
			"func=%d win %d is in space %s [0x%lx..0x%lx], "
			"allocated by prom\n",
			PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot), func, win,
			pci_space[space], (uint64_t)base,
			(uint64_t)(base + size - 1)));

		continue;		/* already allocated */
	    }

	    align = (win) ? size : align_slot; 

	    if (align < PAGE_SIZE)
		align = PAGE_SIZE;        /* ie. 0x00004000 */
 
	    switch (space) {
	    case PCIIO_SPACE_IO:
                base = pcibr_bus_addr_alloc(pcibr_soft,
                                            &pcibr_info->f_window[win],
                                            PCIIO_SPACE_IO,
                                            0, size, align);
                if (!base)
                    rc = ENOSPC;
		break;
		
	    case PCIIO_SPACE_MEM:
		if ((do_pcibr_config_get(wptr, (win * 4), 4) &
		     PCI_BA_MEM_LOCATION) == PCI_BA_MEM_1MEG) {
 
		    /* allocate from 20-bit PCI space */
                    base = pcibr_bus_addr_alloc(pcibr_soft,
                                                &pcibr_info->f_window[win],
                                                PCIIO_SPACE_MEM,
                                                0, size, align);
                    if (!base)
                        rc = ENOSPC;
		} else {
		    /* allocate from 32-bit or 64-bit PCI space */
                    base = pcibr_bus_addr_alloc(pcibr_soft,
                                                &pcibr_info->f_window[win],
                                                PCIIO_SPACE_MEM32,
                                                0, size, align);
		    if (!base) 
			rc = ENOSPC;
		}
		break;
		
	    default:
		base = 0;
		PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_BAR, pcibr_vhdl,
			    "pcibr_slot_addr_space_init: slot=%d, window %d "
			    "had bad space code %d\n", 
			    PCIBR_DEVICE_TO_SLOT(pcibr_soft,slot), win, space));
	    }
	    pcibr_info->f_window[win].w_base = base;
	    do_pcibr_config_set(wptr, (win * 4), 4, base);

	    if (base >= size) {
		PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_BAR, pcibr_vhdl,
			"pcibr_slot_addr_space_init: slot=%d, func=%d. win %d "
			"is in space %s [0x%lx..0x%lx], allocated by pcibr\n",
			PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot), func, win,
			pci_space[space], (uint64_t)base, 
			(uint64_t)(base + size - 1)));
	    } else {
		PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_BAR, pcibr_vhdl,
			"pcibr_slot_addr_space_init: slot=%d, func=%d, win %d, "
			"unable to alloc 0x%lx in space %s\n",
			PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot), func, win,
			(uint64_t)size, pci_space[space]));
	    }
	}				/* next base */

	/*
	 * Allocate space for the EXPANSION ROM
	 */
	base = size = 0;
	{
	    wptr = cfgw + PCI_EXPANSION_ROM / 4;
	    do_pcibr_config_set(wptr, 0, 4, 0xFFFFF000);
	    mask = do_pcibr_config_get(wptr, 0, 4);
	    if (mask & 0xFFFFF000) {
		size = mask & -mask;
                base = pcibr_bus_addr_alloc(pcibr_soft,
                                            &pcibr_info->f_rwindow,
                                            PCIIO_SPACE_MEM32, 
                                            0, size, align);
		if (!base)
		    rc = ENOSPC;
		else {
		    do_pcibr_config_set(wptr, 0, 4, base);
		    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_BAR, pcibr_vhdl,
				"pcibr_slot_addr_space_init: slot=%d, func=%d, "
				"ROM in [0x%X..0x%X], allocated by pcibr\n",
				PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot), 
				func, base, base + size - 1));
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
	 * address when decoding references to its registers so to
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

	pci_cfg_cmd_reg = do_pcibr_config_get(cfgw, PCI_CFG_COMMAND, 4);
	pci_cfg_cmd_reg &= 0xFFFF;
	if (pci_cfg_cmd_reg_add & ~pci_cfg_cmd_reg)
	    do_pcibr_config_set(cfgw, PCI_CFG_COMMAND, 4, 
				pci_cfg_cmd_reg | pci_cfg_cmd_reg_add);
    }				/* next func */
    return rc;
}

/*
 * pcibr_slot_device_init
 * 	Setup the device register in the bridge for this PCI slot.
 */

int
pcibr_slot_device_init(vertex_hdl_t pcibr_vhdl,
		       pciio_slot_t slot)
{
    pcibr_soft_t	 pcibr_soft;
    uint64_t		 devreg;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft)
	return -EINVAL;

    if (!PCIBR_VALID_SLOT(pcibr_soft, slot))
	return -EINVAL;

    /*
     * Adjustments to Device(x) and init of bss_device shadow
     */
    devreg = pcireg_device_get(pcibr_soft, slot);
    devreg &= ~BRIDGE_DEV_PAGE_CHK_DIS;

    /*
     * Enable virtual channels by default (exception: see PIC WAR below)
     */
    devreg |= BRIDGE_DEV_VIRTUAL_EN;

    /*
     * PIC WAR. PV# 855271:  Disable virtual channels in the PIC since
     * it can cause problems with 32-bit devices.  We'll set the bit in
     * pcibr_try_set_device() iff we're 64-bit and requesting virtual 
     * channels.
     */
    if (PCIBR_WAR_ENABLED(PV855271, pcibr_soft)) {
	devreg &= ~BRIDGE_DEV_VIRTUAL_EN;
    }
    devreg |= BRIDGE_DEV_COH;

    pcibr_soft->bs_slot[slot].bss_device = devreg;
    pcireg_device_set(pcibr_soft, slot, devreg);

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DEVREG, pcibr_vhdl,
		"pcibr_slot_device_init: Device(%d): 0x%x\n",
		slot, devreg));
    return 0;
}

/*
 * pcibr_slot_guest_info_init
 *	Setup the host/guest relations for a PCI slot.
 */
int
pcibr_slot_guest_info_init(vertex_hdl_t pcibr_vhdl,
			   pciio_slot_t	slot)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    pcibr_info_t	pcibr_info;
    pcibr_soft_slot_t	slotp;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft)
	return -EINVAL;

    if (!PCIBR_VALID_SLOT(pcibr_soft, slot))
	return -EINVAL;

    slotp = &pcibr_soft->bs_slot[slot];

    /* create info and verticies for guest slots;
     * for compatibilitiy macros, create info
     * for even unpopulated slots (but do not
     * build verticies for them).
     */
    if (pcibr_soft->bs_slot[slot].bss_ninfo < 1) {
	pcibr_infoh = kmalloc(sizeof (*(pcibr_infoh)), GFP_KERNEL);
	if ( !pcibr_infoh ) {
		return -ENOMEM;
	}
	memset(pcibr_infoh, 0, sizeof (*(pcibr_infoh)));

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

    return 0;
}


/*
 * pcibr_slot_call_device_attach
 *	This calls the associated driver attach routine for the PCI
 * 	card in this slot.
 */
int
pcibr_slot_call_device_attach(vertex_hdl_t pcibr_vhdl,
			      pciio_slot_t slot,
			      int          drv_flags)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    pcibr_info_t	pcibr_info;
    int			func;
    vertex_hdl_t	xconn_vhdl, conn_vhdl;
    int			nfunc;
    int                 error_func;
    int                 error_slot = 0;
    int                 error = ENODEV;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft)
	return -EINVAL;

    if (!PCIBR_VALID_SLOT(pcibr_soft, slot))
	return -EINVAL;

    if (pcibr_soft->bs_slot[slot].has_host) {
        return -EPERM;
    }
    
    xconn_vhdl = pcibr_soft->bs_conn;

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;
    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;

    for (func = 0; func < nfunc; ++func) {

	pcibr_info = pcibr_infoh[func];
	
	if (!pcibr_info)
	    continue;

	if (pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE)
	    continue;

	conn_vhdl = pcibr_info->f_vertex;

	error_func = pciio_device_attach(conn_vhdl, drv_flags);

	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DEV_ATTACH, pcibr_vhdl,
		    "pcibr_slot_call_device_attach: slot=%d, func=%d "
		    "drv_flags=0x%x, pciio_device_attach returned %d\n",
		    PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot), func, 
		    drv_flags, error_func));
        pcibr_info->f_att_det_error = error_func;

	if (error_func)
	    error_slot = error_func;

        error = error_slot;

    }				/* next func */

#ifdef CONFIG_HOTPLUG_PCI_SGI
    if (error) {
	if ((error != ENODEV) && (error != EUNATCH) && (error != EPERM)) {
	    pcibr_soft->bs_slot[slot].slot_status &= ~SLOT_STATUS_MASK;
	    pcibr_soft->bs_slot[slot].slot_status |= SLOT_STARTUP_INCMPLT;
	}
    } else {
        pcibr_soft->bs_slot[slot].slot_status &= ~SLOT_STATUS_MASK;
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_STARTUP_CMPLT;
    }
#endif	/* CONFIG_HOTPLUG_PCI_SGI */
    return error;
}

/*
 * pcibr_slot_call_device_detach
 *	This calls the associated driver detach routine for the PCI
 * 	card in this slot.
 */
int
pcibr_slot_call_device_detach(vertex_hdl_t pcibr_vhdl,
			      pciio_slot_t slot,
			      int          drv_flags)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    pcibr_info_t	pcibr_info;
    int			func;
    vertex_hdl_t	conn_vhdl = GRAPH_VERTEX_NONE;
    int			nfunc;
    int                 error_func;
    int                 error_slot = 0;
    int                 error = ENODEV;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft)
	return -EINVAL;

    if (!PCIBR_VALID_SLOT(pcibr_soft, slot))
	return -EINVAL;

    if (pcibr_soft->bs_slot[slot].has_host)
        return -EPERM;

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;
    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;

    for (func = 0; func < nfunc; ++func) {

	pcibr_info = pcibr_infoh[func];
	
	if (!pcibr_info)
	    continue;

	if (pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE)
	    continue;

	if (IS_PCIX(pcibr_soft) && pcibr_info->f_pcix_cap) {
	    int max_out;

	    pcibr_soft->bs_pcix_num_funcs--;
	    max_out = pcibr_info->f_pcix_cap->pcix_type0_status.max_out_split;
	    pcibr_soft->bs_pcix_split_tot -= max_splittrans_to_numbuf[max_out];
	}

	conn_vhdl = pcibr_info->f_vertex;

	error_func = pciio_device_detach(conn_vhdl, drv_flags);

	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DEV_DETACH, pcibr_vhdl,
		    "pcibr_slot_call_device_detach: slot=%d, func=%d "
		    "drv_flags=0x%x, pciio_device_detach returned %d\n",
		    PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot), func, 
		    drv_flags, error_func));

        pcibr_info->f_att_det_error = error_func;

	if (error_func)
	    error_slot = error_func;

	error = error_slot;

    }				/* next func */

#ifdef CONFIG_HOTPLUG_PCI_SGI
    if (error) {
	if ((error != ENODEV) && (error != EUNATCH) && (error != EPERM)) {
	    pcibr_soft->bs_slot[slot].slot_status &= ~SLOT_STATUS_MASK;
            pcibr_soft->bs_slot[slot].slot_status |= SLOT_SHUTDOWN_INCMPLT;
	}
    } else {
        if (conn_vhdl != GRAPH_VERTEX_NONE) 
            pcibr_device_unregister(conn_vhdl);
        pcibr_soft->bs_slot[slot].slot_status &= ~SLOT_STATUS_MASK;
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_SHUTDOWN_CMPLT;
    }
#endif	/* CONFIG_HOTPLUG_PCI_SGI */
    return error;
}



/*
 * pcibr_slot_detach
 *	This is a place holder routine to keep track of all the
 *	slot-specific freeing that needs to be done.
 */
int
pcibr_slot_detach(vertex_hdl_t pcibr_vhdl,
		  pciio_slot_t slot,
		  int          drv_flags,
		  char        *l1_msg,
                  int         *sub_errorp)
{
    pcibr_soft_t  pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    int		  error;
    
    /* Call the device detach function */
    error = (pcibr_slot_call_device_detach(pcibr_vhdl, slot, drv_flags));
    if (error) {
        if (sub_errorp)
            *sub_errorp = error;       
	if (l1_msg)
	    ;
        return PCI_SLOT_DRV_DETACH_ERR;
    }

    /* Recalculate the RBARs for all the devices on the bus since we've
     * just freed some up and some of the devices could use them.
     */
    if (IS_PCIX(pcibr_soft)) {
	int tmp_slot;

	pcibr_soft->bs_pcix_rbar_inuse = 0;
	pcibr_soft->bs_pcix_rbar_avail = NUM_RBAR;
	pcibr_soft->bs_pcix_rbar_percent_allowed = 
					pcibr_pcix_rbars_calc(pcibr_soft);

	for (tmp_slot = pcibr_soft->bs_min_slot;
			tmp_slot < PCIBR_NUM_SLOTS(pcibr_soft); ++tmp_slot)
            (void)pcibr_slot_pcix_rbar_init(pcibr_soft, tmp_slot);
    }

    return 0;

}

/*
 * pcibr_probe_slot_pic: read a config space word
 * while trapping any errors; return zero if
 * all went OK, or nonzero if there was an error.
 * The value read, if any, is passed back
 * through the valp parameter.
 */
static int
pcibr_probe_slot(pcibr_soft_t pcibr_soft,
		 cfg_p cfg,
		 unsigned *valp)
{
    return pcibr_probe_work(pcibr_soft, (void *)cfg, 4, (void *)valp);
}

/*
 * Probe an offset within a piomap with errors disabled.
 * len must be 1, 2, 4, or 8.  	The probed address must be a multiple of
 * len.
 *
 * Returns:	0	if the offset was probed and put valid data in valp
 *		-1	if there was a usage error such as improper alignment
 *			or out of bounds offset/len combination.  In this
 *			case, the map was not probed
 *		1 	if the offset was probed but resulted in an error
 *			such as device not responding, bus error, etc.
 */

int
pcibr_piomap_probe(pcibr_piomap_t piomap, off_t offset, int len, void *valp)
{
	if (offset + len > piomap->bp_mapsz) {
		return -1;
	}

	return pcibr_probe_work(piomap->bp_soft,
				piomap->bp_kvaddr + offset, len, valp);
}

/*
 * pcibr_probe_slot: read a config space word
 * while trapping any errors; return zero if
 * all went OK, or nonzero if there was an error.
 * The value read, if any, is passed back
 * through the valp parameter.
 */
static int
pcibr_probe_work(pcibr_soft_t pcibr_soft,
		 void *addr,
		 int len,
		 void *valp)
{
    int			rv;

    /*
     * Sanity checks ...
     */

    if (len != 1 && len != 2 && len != 4 && len != 8) {
	return -1;				/* invalid len */
    }

    if ((uint64_t)addr & (len-1)) {
	return -1;				/* invalid alignment */
    }

    rv = snia_badaddr_val((void *)addr, len, valp);

    /* Clear the int_view register incase it was set */
    pcireg_intr_reset_set(pcibr_soft, BRIDGE_IRR_MULTI_CLR);

    return (rv ? 1 : 0);	/* return 1 for snia_badaddr_val error, 0 if ok */
}


void
pcibr_device_info_free(vertex_hdl_t pcibr_vhdl, pciio_slot_t slot)
{
    pcibr_soft_t	pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    pcibr_info_t	pcibr_info;
    pciio_function_t	func;
    pcibr_soft_slot_t	slotp = &pcibr_soft->bs_slot[slot];
    cfg_p               cfgw;
    int			nfunc = slotp->bss_ninfo;
    int                 bar;
    int                 devio_index;
    unsigned long	s;
    unsigned            cmd_reg;


    for (func = 0; func < nfunc; func++) {
	pcibr_info = slotp->bss_infos[func];

	if (!pcibr_info) 
	    continue;

        s = pcibr_lock(pcibr_soft);

        /* Disable memory and I/O BARs */
	cfgw = pcibr_func_config_addr(pcibr_soft, 0, slot, func, 0);
	cmd_reg = do_pcibr_config_get(cfgw, PCI_CFG_COMMAND, 4);
	cmd_reg &= (PCI_CMD_MEM_SPACE | PCI_CMD_IO_SPACE);
	do_pcibr_config_set(cfgw, PCI_CFG_COMMAND, 4, cmd_reg);

        for (bar = 0; bar < PCI_CFG_BASE_ADDRS; bar++) {
            if (pcibr_info->f_window[bar].w_space == PCIIO_SPACE_NONE)
                continue;

            /* Free the PCI bus space */
            pcibr_bus_addr_free(&pcibr_info->f_window[bar]);

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

        /* Free the Expansion ROM PCI bus space */
	if(pcibr_info->f_rbase && pcibr_info->f_rsize) {
            pcibr_bus_addr_free(&pcibr_info->f_rwindow);
        }

        pcibr_unlock(pcibr_soft, s);

	slotp->bss_infos[func] = 0;
	pciio_device_info_unregister(pcibr_vhdl, &pcibr_info->f_c);
	pciio_device_info_free(&pcibr_info->f_c);

	kfree(pcibr_info);
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
}


iopaddr_t
pcibr_bus_addr_alloc(pcibr_soft_t pcibr_soft, pciio_win_info_t win_info_p,
                     pciio_space_t space, int start, int size, int align)
{
    pciio_win_map_t win_map_p;
    struct resource *root_resource = NULL;
    iopaddr_t iopaddr = 0;

    switch (space) {

        case PCIIO_SPACE_IO:
            win_map_p = &pcibr_soft->bs_io_win_map;
	    root_resource = &pcibr_soft->bs_io_win_root_resource;
            break;

        case PCIIO_SPACE_MEM:
            win_map_p = &pcibr_soft->bs_swin_map;
	    root_resource = &pcibr_soft->bs_swin_root_resource;
            break;

        case PCIIO_SPACE_MEM32:
            win_map_p = &pcibr_soft->bs_mem_win_map;
	    root_resource = &pcibr_soft->bs_mem_win_root_resource;
            break;

        default:
            return 0;

    }
    iopaddr = pciio_device_win_alloc(root_resource,
				  win_info_p
				  ? &win_info_p->w_win_alloc
				  : NULL,
				  start, size, align);
    return iopaddr;
}


void
pcibr_bus_addr_free(pciio_win_info_t win_info_p)
{
	pciio_device_win_free(&win_info_p->w_win_alloc);
}

/*
 * given a vertex_hdl to the pcibr_vhdl, return the brick's bus number
 * associated with that vertex_hdl.  The true mapping happens from the
 * io_brick_tab[] array defined in ml/SN/iograph.c
 */
int
pcibr_widget_to_bus(vertex_hdl_t pcibr_vhdl) 
{
    pcibr_soft_t	pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    xwidgetnum_t	widget = pcibr_soft->bs_xid;
    int			bricktype = pcibr_soft->bs_bricktype;
    int			bus;
    
    if ((bus = io_brick_map_widget(bricktype, widget)) <= 0) {
	printk(KERN_WARNING "pcibr_widget_to_bus() bad bricktype %d\n", bricktype);
	return 0;
    }

    /* For PIC there are 2 busses per widget and pcibr_soft->bs_busnum
     * will be 0 or 1.  Add in the correct PIC bus offset.
     */
    bus += pcibr_soft->bs_busnum;
    return bus;
}
