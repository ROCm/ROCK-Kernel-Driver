
/******************************************************************************/
/*                                                                            */
/* Broadcom BCM4400 Linux Network Driver, Copyright (c) 2000 Broadcom         */
/* Corporation.                                                               */
/* All rights reserved.                                                       */
/*                                                                            */
/* This program is free software; you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by       */
/* the Free Software Foundation, located in the file LICENSE.                 */
/*                                                                            */
/* /proc file system handling code.                                           */
/*                                                                            */
/******************************************************************************/

#include "b44mm.h"
#ifdef BCM_PROC_FS

#define NICINFO_PROC_DIR "nicinfo"

static struct proc_dir_entry *bcm4400_procfs_dir;

extern char bcm4400_driver[], bcm4400_version[];

#ifdef B44_DEBUG
extern int b44_reset_count;
#endif

static char *na_str = "n/a";
static char *pause_str = "pause ";
static char *asym_pause_str = "asym_pause ";
static char *on_str = "on";
static char *off_str = "off";
static char *up_str = "up";
static char *down_str = "down";

static struct proc_dir_entry *
proc_getdir(char *name, struct proc_dir_entry *proc_dir)
{
	struct proc_dir_entry *pde = proc_dir;

	lock_kernel();
	for (pde=pde->subdir; pde; pde = pde->next) {
		if (pde->namelen && (strcmp(name, pde->name) == 0)) {
			/* directory exists */
			break;
		}
	}
	if (pde == (struct proc_dir_entry *) 0)
	{
		/* create the directory */
#if (LINUX_VERSION_CODE > 0x20300)
		pde = proc_mkdir(name, proc_dir);
#else
		pde = create_proc_entry(name, S_IFDIR, proc_dir);
#endif
		if (pde == (struct proc_dir_entry *) 0) {
			unlock_kernel();
			return (pde);
		}
	}
	unlock_kernel();
	return (pde);
}

int
bcm4400_proc_create(void)
{
	bcm4400_procfs_dir = proc_getdir(NICINFO_PROC_DIR, proc_net);

	if (bcm4400_procfs_dir == (struct proc_dir_entry *) 0) {
		printk(KERN_DEBUG "Could not create procfs nicinfo directory %s\n", NICINFO_PROC_DIR);
		return -1;
	}
	return 0;
}

void
b44_get_speed_adv(PUM_DEVICE_BLOCK pUmDevice, char *str)
{
	PLM_DEVICE_BLOCK pDevice = &pUmDevice->lm_dev;

	if (pDevice->DisableAutoNeg == TRUE) {
		strcpy(str, na_str);
		return;
	}
	str[0] = 0;
	if (pDevice->Advertising & PHY_AN_AD_10BASET_HALF) {
		strcat(str, "10half ");
	}
	if (pDevice->Advertising & PHY_AN_AD_10BASET_FULL) {
		strcat(str, "10full ");
	}
	if (pDevice->Advertising & PHY_AN_AD_100BASETX_HALF) {
		strcat(str, "100half ");
	}
	if (pDevice->Advertising & PHY_AN_AD_100BASETX_FULL) {
		strcat(str, "100full ");
	}
}

void
b44_get_fc_adv(PUM_DEVICE_BLOCK pUmDevice, char *str)
{
	PLM_DEVICE_BLOCK pDevice = &pUmDevice->lm_dev;

	if (pDevice->DisableAutoNeg == TRUE) {
		strcpy(str, na_str);
		return;
	}
	str[0] = 0;
	if (pDevice->Advertising & PHY_AN_AD_PAUSE_CAPABLE) {
		strcat(str, pause_str);
	}
	if (pDevice->Advertising & PHY_AN_AD_ASYM_PAUSE) {
		strcat(str, asym_pause_str);
	}
}

int
bcm4400_read_pfs(char *page, char **start, off_t off, int count,
	int *eof, void *data)
{
	struct net_device *dev = (struct net_device *) data;
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) dev->priv;
	PLM_DEVICE_BLOCK pDevice = &pUmDevice->lm_dev;
	int len = 0;
	unsigned long rx_mac_errors, rx_crc_errors, rx_align_errors;
	unsigned long rx_runt_errors, rx_frag_errors, rx_long_errors;
	unsigned long rx_overrun_errors, rx_jabber_errors;
	char str[64];

	len += sprintf(page+len, "Description\t\t\t%s\n", pUmDevice->name);
	len += sprintf(page+len, "Driver_Name\t\t\t%s\n", bcm4400_driver);
	len += sprintf(page+len, "Driver_Version\t\t\t%s\n", bcm4400_version);
	len += sprintf(page+len, "PCI_Vendor\t\t\t0x%04x\n", pDevice->PciVendorId);
	len += sprintf(page+len, "PCI_Device_ID\t\t\t0x%04x\n",
		pDevice->PciDeviceId);
	len += sprintf(page+len, "PCI_Subsystem_Vendor\t\t0x%04x\n",
		pDevice->PciSubvendorId);
	len += sprintf(page+len, "PCI_Subsystem_ID\t\t0x%04x\n",
		pDevice->PciSubsystemId);
	len += sprintf(page+len, "PCI_Revision_ID\t\t\t0x%02x\n",
		pDevice->PciRevId);
	len += sprintf(page+len, "PCI_Slot\t\t\t%d\n",
		PCI_SLOT(pUmDevice->pdev->devfn));
	len += sprintf(page+len, "PCI_Bus\t\t\t\t%d\n",
		pUmDevice->pdev->bus->number);

	len += sprintf(page+len, "Memory\t\t\t\t0x%lx\n", pUmDevice->dev->base_addr);
	len += sprintf(page+len, "IRQ\t\t\t\t%d\n", dev->irq);
	len += sprintf(page+len, "System_Device_Name\t\t%s\n", dev->name);
	len += sprintf(page+len, "Current_HWaddr\t\t\t%02x:%02x:%02x:%02x:%02x:%02x\n",
		dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);
	len += sprintf(page+len,
		"Permanent_HWaddr\t\t%02x:%02x:%02x:%02x:%02x:%02x\n",
		pDevice->NodeAddress[0], pDevice->NodeAddress[1],
		pDevice->NodeAddress[2], pDevice->NodeAddress[3],
		pDevice->NodeAddress[4], pDevice->NodeAddress[5]);

	len += sprintf(page+len, "Link\t\t\t\t%s\n", 
		(pUmDevice->opened == 0) ? "unknown" :
    		((pDevice->LinkStatus == LM_STATUS_LINK_ACTIVE) ? up_str :
		down_str));
	len += sprintf(page+len, "Auto_Negotiate\t\t\t%s\n", 
    		(pDevice->DisableAutoNeg == TRUE) ? off_str : on_str);
	b44_get_speed_adv(pUmDevice, str);
	len += sprintf(page+len, "Speed_Advertisement\t\t%s\n", str);
	b44_get_fc_adv(pUmDevice, str);
	len += sprintf(page+len, "Flow_Control_Advertisement\t%s\n", str);
	len += sprintf(page+len, "Speed\t\t\t\t%s\n", 
    		((pDevice->LinkStatus == LM_STATUS_LINK_DOWN) ||
		(pUmDevice->opened == 0)) ? na_str :
    		((pDevice->LineSpeed == LM_LINE_SPEED_100MBPS) ? "100" :
    		(pDevice->LineSpeed == LM_LINE_SPEED_10MBPS) ? "10" : na_str));
	len += sprintf(page+len, "Duplex\t\t\t\t%s\n", 
    		((pDevice->LinkStatus == LM_STATUS_LINK_DOWN) ||
		(pUmDevice->opened == 0)) ? na_str :
		((pDevice->DuplexMode == LM_DUPLEX_MODE_FULL) ? "full" :
			"half"));
	len += sprintf(page+len, "Flow_Control\t\t\t%s\n", 
    		((pDevice->LinkStatus == LM_STATUS_LINK_DOWN) ||
		(pUmDevice->opened == 0)) ? na_str :
		((pDevice->FlowControl == LM_FLOW_CONTROL_NONE) ? off_str :
		(((pDevice->FlowControl & LM_FLOW_CONTROL_RX_TX_PAUSE) ==
			LM_FLOW_CONTROL_RX_TX_PAUSE) ? "receive/transmit" :
		(pDevice->FlowControl & LM_FLOW_CONTROL_RECEIVE_PAUSE) ?
			"receive" : "transmit")));
	len += sprintf(page+len, "State\t\t\t\t%s\n", 
    		(dev->flags & IFF_UP) ? up_str : down_str);
	len += sprintf(page+len, "MTU_Size\t\t\t%d\n\n", dev->mtu);
	len += sprintf(page+len, "Rx_Packets\t\t\t%lu\n", pDevice->rx_pkts);

	len += sprintf(page+len, "Tx_Packets\t\t\t%lu\n", pDevice->tx_pkts);
	len += sprintf(page+len, "Rx_Bytes\t\t\t%lu\n", pDevice->rx_octets);
	len += sprintf(page+len, "Tx_Bytes\t\t\t%lu\n", pDevice->tx_octets);
	rx_align_errors = pDevice->rx_align_errs;
	rx_crc_errors = pDevice->rx_crc_errs;
	rx_runt_errors = pDevice->rx_undersize;
	rx_frag_errors = pDevice->rx_fragment_pkts;
	rx_long_errors = pDevice->rx_oversize_pkts;
	rx_overrun_errors = pDevice->rx_missed_pkts;
	rx_jabber_errors = pDevice->rx_jabber_pkts;
	rx_mac_errors = rx_crc_errors + rx_align_errors + rx_runt_errors +
		rx_frag_errors + rx_long_errors + rx_jabber_errors;
	len += sprintf(page+len, "Rx_Errors\t\t\t%lu\n",
		rx_mac_errors + rx_overrun_errors);
	len += sprintf(page+len, "Tx_Errors\t\t\t%lu\n",
		pDevice->tx_jabber_pkts + pDevice->tx_oversize_pkts +
		pDevice->tx_underruns + pDevice->tx_excessive_cols +
		pDevice->tx_late_cols);
	len += sprintf(page+len, "\nTx_Carrier_Errors\t\t%lu\n",
		pDevice->tx_carrier_lost);
	len += sprintf(page+len, "Tx_Abort_Excess_Coll\t\t%lu\n",
		pDevice->tx_excessive_cols);
	len += sprintf(page+len, "Tx_Abort_Late_Coll\t\t%lu\n",
		pDevice->tx_late_cols);
	len += sprintf(page+len, "Tx_Deferred_Ok\t\t\t%lu\n",
		pDevice->tx_defered);
	len += sprintf(page+len, "Tx_Single_Coll_Ok\t\t%lu\n",
		pDevice->tx_single_cols);
	len += sprintf(page+len, "Tx_Multi_Coll_Ok\t\t%lu\n",
		pDevice->tx_multiple_cols);
	len += sprintf(page+len, "Tx_Total_Coll_Ok\t\t%lu\n",
		pDevice->tx_total_cols);
	len += sprintf(page+len, "\nRx_CRC_Errors\t\t\t%lu\n", 
		rx_crc_errors);
	len += sprintf(page+len, "Rx_Short_Fragment_Errors\t%lu\n",
		rx_frag_errors);
	len += sprintf(page+len, "Rx_Short_Length_Errors\t\t%lu\n",
		rx_runt_errors);
	len += sprintf(page+len, "Rx_Long_Length_Errors\t\t%lu\n",
		rx_long_errors);
	len += sprintf(page+len, "Rx_Align_Errors\t\t\t%lu\n",
		rx_align_errors);
	len += sprintf(page+len, "Rx_Overrun_Errors\t\t%lu\n",
		rx_overrun_errors);
	len += sprintf(page+len, "\nTx_MAC_Errors\t\t\t%lu\n",
		pDevice->tx_underruns);
	len += sprintf(page+len, "Rx_MAC_Errors\t\t\t%lu\n\n",
		rx_mac_errors);

	len += sprintf(page+len, "Tx_Desc_Count\t\t\t%u\n",
		pDevice->TxPacketDescCnt);
	len += sprintf(page+len, "Rx_Desc_Count\t\t\t%u\n\n",
		pDevice->RxPacketDescCnt);

#ifdef BCM_WOL
	len += sprintf(page+len, "Wake_On_LAN\t\t\t%s\n",
        	((pDevice->WakeUpMode & LM_WAKE_UP_MODE_MAGIC_PACKET) ?
		on_str : off_str));
#endif
#ifdef B44_DEBUG
	len += sprintf(page+len, "Intr_Sem\t\t\t%u\n",
		atomic_read(&pUmDevice->intr_sem));
	len += sprintf(page+len, "Int_Status\t\t\t%x\n",
		REG_RD(pDevice, intstatus));
	len += sprintf(page+len, "Int_Mask\t\t\t%x\n",
		REG_RD(pDevice, intmask));
	len += sprintf(page+len, "Reset_Count\t\t\t%u\n", b44_reset_count);
#endif

	*eof = 1;
	return len;
}

int
bcm4400_proc_create_dev(struct net_device *dev)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) dev->priv;

	if (!bcm4400_procfs_dir)
		return -1;

	sprintf(pUmDevice->pfs_name, "%s.info", dev->name);
	pUmDevice->pfs_entry = create_proc_entry(pUmDevice->pfs_name,
		S_IFREG, bcm4400_procfs_dir);
	if (pUmDevice->pfs_entry == 0)
		return -1;
	pUmDevice->pfs_entry->read_proc = bcm4400_read_pfs;
	pUmDevice->pfs_entry->data = dev;
	return 0;
}
int
bcm4400_proc_remove_dev(struct net_device *dev)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) dev->priv;

	remove_proc_entry(pUmDevice->pfs_name, bcm4400_procfs_dir);
	return 0;
}

#endif
