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

#ifdef LATER

char *pci_space[] = {"NONE", 
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
idbg_pss_func(pcibr_info_h pcibr_infoh, int func)
{
    pcibr_info_t	pcibr_info = pcibr_infoh[func];
    char		name[MAXDEVNAME];
    int			win;
    
    if (!pcibr_info)
	return;
    qprintf("Per-slot Function Info\n");
    sprintf(name, "%v", pcibr_info->f_vertex);
    qprintf("\tSlot Name : %s\n",name);
    qprintf("\tPCI Bus : %d ",pcibr_info->f_bus);
    qprintf("Slot : %d ", pcibr_info->f_slot);
    qprintf("Function : %d ", pcibr_info->f_func);
    qprintf("VendorId : 0x%x " , pcibr_info->f_vendor);
    qprintf("DeviceId : 0x%x\n", pcibr_info->f_device);
    sprintf(name, "%v", pcibr_info->f_master);
    qprintf("\tBus provider : %s\n",name);
    qprintf("\tProvider Fns : 0x%x ", pcibr_info->f_pops);
    qprintf("Error Handler : 0x%x Arg 0x%x\n", 
	    pcibr_info->f_efunc,pcibr_info->f_einfo);
    for(win = 0 ; win < 6 ; win++) 
	qprintf("\tBase Reg #%d space %s base 0x%x size 0x%x\n",
		win,pci_space[pcibr_info->f_window[win].w_space],
		pcibr_info->f_window[win].w_base,
		pcibr_info->f_window[win].w_size);

    qprintf("\tRom base 0x%x size 0x%x\n", 
	    pcibr_info->f_rbase,pcibr_info->f_rsize);

    qprintf("\tInterrupt Bit Map\n");
    qprintf("\t\tPCI Int#\tBridge Pin#\n");
    for (win = 0 ; win < 4; win++)
	qprintf("\t\tINT%c\t\t%d\n",win+'A',pcibr_info->f_ibit[win]);
    qprintf("\n");
}


void
idbg_pss_info(pcibr_soft_t pcibr_soft, pciio_slot_t slot)
{
    pcibr_soft_slot_t	pss;
    char		slot_conn_name[MAXDEVNAME];
    int			func;

    pss = &pcibr_soft->bs_slot[slot];
    qprintf("PCI INFRASTRUCTURAL INFO FOR SLOT %d\n", slot);
    qprintf("\tHost Present ? %s ", pss->has_host ? "yes" : "no");
    qprintf("\tHost Slot : %d\n",pss->host_slot);
    sprintf(slot_conn_name, "%v", pss->slot_conn);
    qprintf("\tSlot Conn : %s\n",slot_conn_name);	
    qprintf("\t#Functions : %d\n",pss->bss_ninfo);
    for (func = 0; func < pss->bss_ninfo; func++)
	idbg_pss_func(pss->bss_infos,func);
    qprintf("\tSpace : %s ",pci_space[pss->bss_devio.bssd_space]);
    qprintf("\tBase : 0x%x ", pss->bss_devio.bssd_base);
    qprintf("\tShadow Devreg : 0x%x\n", pss->bss_device);
    qprintf("\tUsage counts : pmu %d d32 %d d64 %d\n",
	    pss->bss_pmu_uctr,pss->bss_d32_uctr,pss->bss_d64_uctr);
    
    qprintf("\tDirect Trans Info : d64_base 0x%x d64_flags 0x%x"
	    "d32_base 0x%x d32_flags 0x%x\n",
	    pss->bss_d64_base, pss->bss_d64_flags,
	    pss->bss_d32_base, pss->bss_d32_flags);
    
    qprintf("\tExt ATEs active ? %s", 
	    pss->bss_ext_ates_active ? "yes" : "no");
    qprintf(" Command register : 0x%x ", pss->bss_cmd_pointer);
    qprintf(" Shadow command val : 0x%x\n", pss->bss_cmd_shadow);

    qprintf("\tRRB Info : Valid %d+%d Reserved %d\n",
	    pcibr_soft->bs_rrb_valid[slot],
	    pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL],
	    pcibr_soft->bs_rrb_res[slot]);
		
}

int	ips = 0;

void
idbg_pss(pcibr_soft_t pcibr_soft)
{
    pciio_slot_t	slot;

    
    if (ips >= 0 && ips < 8)
	idbg_pss_info(pcibr_soft,ips);
    else if (ips < 0)
	for (slot = 0; slot < 8; slot++) 
	    idbg_pss_info(pcibr_soft,slot);
    else
	qprintf("Invalid ips %d\n",ips);
}
#endif	/* LATER */
