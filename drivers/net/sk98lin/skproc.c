/******************************************************************************
 *
 * Name:    skproc.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.2.2.2 $
 * Date:    $Date: 2001/03/15 12:50:13 $
 * Purpose:	Funktions to display statictic data
 *
 ******************************************************************************/
 
/******************************************************************************
 *
 *	(C)Copyright 1998-2001 SysKonnect GmbH.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Created 22-Nov-2000
 *	Author: Mirko Lindner (mlindner@syskonnect.de)
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/
/******************************************************************************
 *
 * History:
 *
 *	$Log: skproc.c,v $
 *	Revision 1.2.2.2  2001/03/15 12:50:13  mlindner
 *	fix: ProcFS owner protection
 *	
 *	Revision 1.2.2.1  2001/03/12 16:43:48  mlindner
 *	chg: 2.4 requirements for procfs
 *	
 *	Revision 1.1  2001/01/22 14:15:31  mlindner
 *	added ProcFs functionality
 *	Dual Net functionality integrated
 *	Rlmt networks added
 *	
 *
 ******************************************************************************/

#include <linux/proc_fs.h>

#include "h/skdrv1st.h"
#include "h/skdrv2nd.h"

extern spinlock_t sk_devs_lock;

static int sk_show_dev(struct net_device *dev, char *buf)
{
	DEV_NET	*pNet = (DEV_NET*) dev->priv;
	SK_AC *pAC = pNet->pAC;
	int t = pNet->PortNr;
	SK_RLMT_NET *rlmt = &pAC->Rlmt.Net[t];
	unsigned long Flags;		
	unsigned Size;
	int len = 0;
	int i;

	SK_PNMI_STRUCT_DATA 	*pPnmiStruct = &pAC->PnmiStruct;
	SK_PNMI_STAT		*pPnmiStat;

	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	Size = SK_PNMI_STRUCT_SIZE;
	SkPnmiGetStruct(pAC, pAC->IoBase, pPnmiStruct, &Size, t);
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);

	pPnmiStat = &pPnmiStruct->Stat[0];

	len = sprintf(buf, "\nDetailed statistic for device %s\n", dev->name);
	len += sprintf(buf + len, "==================================\n");

	/* Board statistics */
	len += sprintf(buf + len, "\nBoard statistics\n\n");
	len += sprintf(buf + len, "Active Port               %c\n",
		'A' + rlmt->Port[rlmt->ActivePort]->PortNumber);
	len += sprintf(buf + len, "Preferred Port            %c\n",
		'A' + rlmt->Port[rlmt->PrefPort]->PortNumber);

	len += sprintf(buf + len, "Bus speed (Mhz)           %d\n",
		pPnmiStruct->BusSpeed);

	len += sprintf(buf + len, "Bus width (Bit)           %d\n",
		pPnmiStruct->BusWidth);

	for (i=0; i < SK_MAX_SENSORS; i ++) {
		SK_SENSOR *sens = &pAC->I2c.SenTable[i];
		SK_I32 val = sens->SenValue;
		if (strcmp(sens->SenDesc, "Temperature") == 0 ) {
			len += sprintf(buf + len,
				"Temperature (C)           %d.%d\n",
				val / 10, val % 10);
			val = val * 18 + 3200;
			len += sprintf(buf + len,
				"Temperature (F)           %d.%d\n",
				val/100, val % 10);
		} else if (strcmp(sens->SenDesc, "Speed Fan") == 0 ) {
			len += sprintf(buf + len,
				"Speed Fan                 %d\n",
				val);
		} else {
			len += sprintf(buf + len,
				"%-20s      %d.%d\n",
				sens->SenDesc, val / 1000, val % 1000);
		}
	}
	
	/*Receive statistics */
	
	len += sprintf(buf + len, "\nReceive statistics\n\n");

	len += sprintf(buf + len, "Received bytes            %Ld\n",
		(unsigned long long) pPnmiStat->StatRxOctetsOkCts);
	len += sprintf(buf + len, "Received packets          %Ld\n",
		(unsigned long long) pPnmiStat->StatRxOkCts);
	len += sprintf(buf + len, "Received errors           %Ld\n",
		(unsigned long long) pPnmiStat->StatRxFcsCts);
	len += sprintf(buf + len, "Received dropped          %Ld\n",
		(unsigned long long) pPnmiStruct->RxNoBufCts);
	len += sprintf(buf + len, "Received multicast        %Ld\n",
		(unsigned long long) pPnmiStat->StatRxMulticastOkCts);
	len += sprintf(buf + len, "Received errors types\n");
	len += sprintf(buf + len, "   length errors          %Ld\n",
		(unsigned long long) pPnmiStat->StatRxRuntCts);
	len += sprintf(buf + len, "   over errors            %Ld\n",
		(unsigned long long) pPnmiStat->StatRxFifoOverflowCts);
	len += sprintf(buf + len, "   crc errors             %Ld\n",
		(unsigned long long) pPnmiStat->StatRxFcsCts);
	len += sprintf(buf + len, "   frame errors           %Ld\n",
		(unsigned long long) pPnmiStat->StatRxFramingCts);
	len += sprintf(buf + len, "   fifo errors            %Ld\n",
		(unsigned long long) pPnmiStat->StatRxFifoOverflowCts);
	len += sprintf(buf + len, "   missed errors          %Ld\n",
		(unsigned long long) pPnmiStat->StatRxMissedCts);
	
	/*Transmit statistics */
	len += sprintf(buf + len, "\nTransmit statistics\n\n");
	
	len += sprintf(buf + len, "Transmit bytes            %Ld\n",
		(unsigned long long) pPnmiStat->StatTxOctetsOkCts);
	len += sprintf(buf + len, "Transmit packets          %Ld\n",
		(unsigned long long) pPnmiStat->StatTxOkCts);
	len += sprintf(buf + len, "Transmit errors           %Ld\n",
		(unsigned long long) pPnmiStat->StatTxSingleCollisionCts);
	len += sprintf(buf + len, "Transmit dropped          %Ld\n",
		(unsigned long long) pPnmiStruct->TxNoBufCts);
	len += sprintf(buf + len, "Transmit collisions       %Ld\n",
		(unsigned long long) pPnmiStat->StatTxSingleCollisionCts);
	len += sprintf(buf + len, "Transmited errors types\n");
	len += sprintf(buf + len, "   aborted errors         %ld\n",
		pAC->stats.tx_aborted_errors);
	len += sprintf(buf + len, "   carrier errors         %Ld\n",
		(unsigned long long) pPnmiStat->StatTxCarrierCts);
	len += sprintf(buf + len, "   fifo errors            %Ld\n",
		(unsigned long long) pPnmiStat->StatTxFifoUnderrunCts);
	len += sprintf(buf + len, "   heartbeat errors       %Ld\n",
		(unsigned long long) pPnmiStat->StatTxCarrierCts);
	len += sprintf(buf + len, "   window errors          %ld\n",
		pAC->stats.tx_window_errors);
	return len;
}

static ssize_t sk_read(struct file *file, char *buf, size_t nbytes, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
	struct proc_dir_entry *entry = PDE(inode);
	char *page = (char *)__get_free_page(GFP_KERNEL);
	struct net_device *dev;
	loff_t pos = *ppos;
	ssize_t res = 0;
	int len = 0;

	if (!page)
		return -ENOMEM;

	spin_lock(&sk_devs_lock);
	dev = entry->data;
	if (dev)
		len = sk_show_dev(dev, page);
	spin_unlock(&sk_devs_lock);

	if (pos >= 0 && pos < len) {
		res = nbytes;
		if (res > len - pos)
			res = len - pos;
		if (copy_to_user(page + pos, buf, nbytes))
			res = -EFAULT;
		else
			*ppos = pos + res;
	}
	free_page((unsigned long) page);
	return nbytes;
}

static loff_t sk_lseek(struct file *file, loff_t offset, int orig)
{
	switch (orig) {
	    case 1:
		offset += file->f_pos;
	    case 0:
		if (offset >= 0)
			return file->f_pos = offset;
	}
	return -EINVAL;
}

struct file_operations sk_proc_fops = {
	.read = sk_read,
	.llseek	= sk_lseek,
};
