/******************************************************************************
 *
 * Name:    skproc.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.2 $
 * Date:    $Date: 2003/08/12 16:45:29 $
 * Purpose:	Funktions to display statictic data
 *
 ******************************************************************************/
 
/******************************************************************************
 *
 *	(C)Copyright 1998-2003 SysKonnect GmbH.
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
 *	Revision 1.2  2003/08/12 16:45:29  mlindner
 *	Add: Removed SkNumber and SkDoDiv
 *	Add: Counter output as (unsigned long long)
 *	
 *	Revision 1.1  2003/07/18 13:39:57  rroesler
 *	Fix: Re-enter after CVS crash
 *	
 *	Revision 1.8  2003/06/27 14:41:42  rroesler
 *	Corrected compiler-warning kernel 2.2
 *	
 *	Revision 1.7  2003/06/27 12:09:51  rroesler
 *	corrected minor edits
 *	
 *	Revision 1.6  2003/05/26 12:58:53  mlindner
 *	Add: Support for Kernel 2.5/2.6
 *	
 *	Revision 1.5  2003/03/19 14:40:47  mlindner
 *	Fix: Editorial changes
 *	
 *	Revision 1.4  2003/02/25 14:16:37  mlindner
 *	Fix: Copyright statement
 *	
 *	Revision 1.3  2002/10/02 12:59:51  mlindner
 *	Add: Support for Yukon
 *	Add: Speed check and setup
 *	Add: Merge source for kernel 2.2.x and 2.4.x
 *	Add: Read sensor names directly from VPD
 *	Fix: Volt values
 *	
 *	Revision 1.2.2.7  2002/01/14 12:45:15  mlindner
 *	Fix: Editorial changes
 *	
 *	Revision 1.2.2.6  2001/12/06 15:26:07  mlindner
 *	Fix: Return value of proc_read
 *	
 *	Revision 1.2.2.5  2001/12/06 09:57:39  mlindner
 *	New ProcFs entries
 *	
 *	Revision 1.2.2.4  2001/09/05 12:16:02  mlindner
 *	Add: New ProcFs entries
 *	Fix: Counter Errors (Jumbo == to long errors)
 *	Fix: Kernel error compilation
 *	Fix: too short counters
 *	
 *	Revision 1.2.2.3  2001/06/25 07:26:26  mlindner
 *	Add: More error messages
 *	
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
#include <linux/seq_file.h>

#include "h/skdrv1st.h"
#include "h/skdrv2nd.h"

#ifdef CONFIG_PROC_FS

extern struct net_device	*SkGeRootDev;

static int sk_seq_show(struct seq_file *seq, void *v)
{
	struct net_device *dev = seq->private;
	DEV_NET		*pNet = dev->priv;
	SK_AC		*pAC = pNet->pAC;
	SK_PNMI_STRUCT_DATA *pPnmiStruct = &pAC->PnmiStruct;
	SK_PNMI_STAT	*pPnmiStat = &pPnmiStruct->Stat[0];
	int unit = !(pAC->dev[0] == dev);
	int i;
	char sens_msg[50];

	seq_printf(seq,
		   "\nDetailed statistic for device %s\n",
		   dev->name);
	seq_printf(seq,
		   "=======================================\n");
	
	/* Board statistics */
	seq_printf(seq, 
		   "\nBoard statistics\n\n");
	seq_printf(seq,
		   "Active Port                    %c\n",
		   'A' + pAC->Rlmt.Net[unit].Port[pAC->Rlmt.
						 Net[unit].PrefPort]->PortNumber);
	seq_printf(seq,
		   "Preferred Port                 %c\n",
		   'A' + pAC->Rlmt.Net[unit].Port[pAC->Rlmt.
						 Net[unit].PrefPort]->PortNumber);

	seq_printf(seq,
		   "Bus speed (MHz)                %d\n",
		   pPnmiStruct->BusSpeed);

	seq_printf(seq,
		   "Bus width (Bit)                %d\n",
		   pPnmiStruct->BusWidth);
	seq_printf(seq,
		   "Hardware revision              v%d.%d\n",
		   (pAC->GIni.GIPciHwRev >> 4) & 0x0F,
		   pAC->GIni.GIPciHwRev & 0x0F);

	/* Print sensor informations */
	for (i=0; i < pAC->I2c.MaxSens; i ++) {
		/* Check type */
		switch (pAC->I2c.SenTable[i].SenType) {
		case 1:
			strcpy(sens_msg, pAC->I2c.SenTable[i].SenDesc);
			strcat(sens_msg, " (C)");
			seq_printf(seq,
				   "%-25s      %d.%02d\n",
				   sens_msg,
				   pAC->I2c.SenTable[i].SenValue / 10,
				   pAC->I2c.SenTable[i].SenValue % 10);

			strcpy(sens_msg, pAC->I2c.SenTable[i].SenDesc);
			strcat(sens_msg, " (F)");
			seq_printf(seq,
				   "%-25s      %d.%02d\n",
				   sens_msg,
				   ((((pAC->I2c.SenTable[i].SenValue)
				      *10)*9)/5 + 3200)/100,
				   ((((pAC->I2c.SenTable[i].SenValue)
				      *10)*9)/5 + 3200) % 10);
			break;
		case 2:
			strcpy(sens_msg, pAC->I2c.SenTable[i].SenDesc);
			strcat(sens_msg, " (V)");
			seq_printf(seq,
				   "%-25s      %d.%03d\n",
				   sens_msg,
				   pAC->I2c.SenTable[i].SenValue / 1000,
				   pAC->I2c.SenTable[i].SenValue % 1000);
			break;
		case 3:
			strcpy(sens_msg, pAC->I2c.SenTable[i].SenDesc);
			strcat(sens_msg, " (rpm)");
			seq_printf(seq,
				   "%-25s      %d\n",
				   sens_msg,
				   pAC->I2c.SenTable[i].SenValue);
			break;
		default:
			break;
		}
	}
				
	/*Receive statistics */
	seq_printf(seq, 
		   "\nReceive statistics\n\n");

	seq_printf(seq,
		   "Received bytes                 %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxOctetsOkCts);
	seq_printf(seq,
		   "Received packets               %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxOkCts);
#if 0
	if (pAC->GIni.GP[0].PhyType == SK_PHY_XMAC && 
	    pAC->HWRevision < 12) {
		pPnmiStruct->InErrorsCts = pPnmiStruct->InErrorsCts - 
			pPnmiStat->StatRxShortsCts;
		pPnmiStat->StatRxShortsCts = 0;
	}
#endif
	if (pNet->Mtu > 1500) 
		pPnmiStruct->InErrorsCts = pPnmiStruct->InErrorsCts -
			pPnmiStat->StatRxTooLongCts;

	seq_printf(seq,
		   "Receive errors                 %Ld\n",
		   (unsigned long long) pPnmiStruct->InErrorsCts);
	seq_printf(seq,
		   "Receive dropped                %Ld\n",
		   (unsigned long long) pPnmiStruct->RxNoBufCts);
	seq_printf(seq,
		   "Received multicast             %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxMulticastOkCts);
	seq_printf(seq,
		   "Receive error types\n");
	seq_printf(seq,
		   "   length                      %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxRuntCts);
	seq_printf(seq,
		   "   buffer overflow             %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxFifoOverflowCts);
	seq_printf(seq,
		   "   bad crc                     %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxFcsCts);
	seq_printf(seq,
		   "   framing                     %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxFramingCts);
	seq_printf(seq,
		   "   missed frames               %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxMissedCts);

	if (pNet->Mtu > 1500)
		pPnmiStat->StatRxTooLongCts = 0;

	seq_printf(seq,
		   "   too long                    %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxTooLongCts);					
	seq_printf(seq,
		   "   carrier extension           %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxCextCts);				
	seq_printf(seq,
		   "   too short                   %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxShortsCts);				
	seq_printf(seq,
		   "   symbol                      %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxSymbolCts);				
	seq_printf(seq,
		   "   LLC MAC size                %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxIRLengthCts);				
	seq_printf(seq,
		   "   carrier event               %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxCarrierCts);				
	seq_printf(seq,
		   "   jabber                      %Ld\n",
		   (unsigned long long) pPnmiStat->StatRxJabberCts);				


	/*Transmit statistics */
	seq_printf(seq, 
		   "\nTransmit statistics\n\n");
				
	seq_printf(seq,
		   "Transmited bytes               %Ld\n",
		   (unsigned long long) pPnmiStat->StatTxOctetsOkCts);
	seq_printf(seq,
		   "Transmited packets             %Ld\n",
		   (unsigned long long) pPnmiStat->StatTxOkCts);
	seq_printf(seq,
		   "Transmit errors                %Ld\n",
		   (unsigned long long) pPnmiStat->StatTxSingleCollisionCts);
	seq_printf(seq,
		   "Transmit dropped               %Ld\n",
		   (unsigned long long) pPnmiStruct->TxNoBufCts);
	seq_printf(seq,
		   "Transmit collisions            %Ld\n",
		   (unsigned long long) pPnmiStat->StatTxSingleCollisionCts);
	seq_printf(seq,
		   "Transmit error types\n");
	seq_printf(seq,
		   "   excessive collision         %ld\n",
		   pAC->stats.tx_aborted_errors);
	seq_printf(seq,
		   "   carrier                     %Ld\n",
		   (unsigned long long) pPnmiStat->StatTxCarrierCts);
	seq_printf(seq,
		   "   fifo underrun               %Ld\n",
		   (unsigned long long) pPnmiStat->StatTxFifoUnderrunCts);
	seq_printf(seq,
		   "   heartbeat                   %Ld\n",
		   (unsigned long long) pPnmiStat->StatTxCarrierCts);
	seq_printf(seq,
		   "   window                      %ld\n",
		   pAC->stats.tx_window_errors);
				
	return 0;
}


static int sk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, sk_seq_show, PDE(inode)->data);
}

struct file_operations sk_proc_fops = {
	.owner = THIS_MODULE,
	.open  = sk_proc_open,
	.read  = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif
