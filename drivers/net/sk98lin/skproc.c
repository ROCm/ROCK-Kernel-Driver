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

#include "h/skdrv1st.h"
#include "h/skdrv2nd.h"

	extern struct net_device	*SkGeRootDev;


int sk_proc_read(char *buffer,
				char **buffer_location,
				off_t offset,
				int buffer_length,
				int *eof,
				void *data);



/*****************************************************************************
 *
 * 	sk_proc_read - print "summaries" entry 
 *
 * Description:
 *  This function fills the proc entry with statistic data about 
 *  the ethernet device.
 *  
 *
 * Returns: buffer with statistic data
 *	
 */
int sk_proc_read(char *buffer,
char **buffer_location,
off_t offset,
int buffer_length,
int *eof,
void *data)
{
	int len = 0;
	int t;
	int i;
	DEV_NET					*pNet;
	SK_AC					*pAC;
	char					sens_msg[50];
	unsigned long			Flags;	
	unsigned int			Size;
	struct SK_NET_DEVICE 		*next;
	struct SK_NET_DEVICE 		*SkgeProcDev = SkGeRootDev;

	SK_PNMI_STRUCT_DATA 	*pPnmiStruct;
	SK_PNMI_STAT		*pPnmiStat;
	struct proc_dir_entry *file = (struct proc_dir_entry*) data;

	while (SkgeProcDev) {
		pNet = (DEV_NET*) SkgeProcDev->priv;
		pAC = pNet->pAC;
		next = pAC->Next;
		pPnmiStruct = &pAC->PnmiStruct;
		/* NetIndex in GetStruct is now required, zero is only dummy */

		for (t=pAC->GIni.GIMacsFound; t > 0; t--) {
			if ((pAC->GIni.GIMacsFound == 2) && pAC->RlmtNets == 1)
				t--;

			spin_lock_irqsave(&pAC->SlowPathLock, Flags);
			Size = SK_PNMI_STRUCT_SIZE;
			SkPnmiGetStruct(pAC, pAC->IoBase, 
				pPnmiStruct, &Size, t-1);
			spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	
			if (strcmp(pAC->dev[t-1]->name, file->name) == 0) {
				pPnmiStat = &pPnmiStruct->Stat[0];
				len = sprintf(buffer, 
					"\nDetailed statistic for device %s\n",
					pAC->dev[t-1]->name);
				len += sprintf(buffer + len,
					"=======================================\n");
	
				/* Board statistics */
				len += sprintf(buffer + len, 
					"\nBoard statistics\n\n");
				len += sprintf(buffer + len,
					"Active Port                    %c\n",
					'A' + pAC->Rlmt.Net[t-1].Port[pAC->Rlmt.
					Net[t-1].PrefPort]->PortNumber);
				len += sprintf(buffer + len,
					"Preferred Port                 %c\n",
					'A' + pAC->Rlmt.Net[t-1].Port[pAC->Rlmt.
					Net[t-1].PrefPort]->PortNumber);

				len += sprintf(buffer + len,
					"Bus speed (MHz)                %d\n",
					pPnmiStruct->BusSpeed);

				len += sprintf(buffer + len,
					"Bus width (Bit)                %d\n",
					pPnmiStruct->BusWidth);
				len += sprintf(buffer + len,
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
						len += sprintf(buffer + len,
							"%-25s      %d.%02d\n",
							sens_msg,
							pAC->I2c.SenTable[i].SenValue / 10,
							pAC->I2c.SenTable[i].SenValue % 10);

						strcpy(sens_msg, pAC->I2c.SenTable[i].SenDesc);
						strcat(sens_msg, " (F)");
						len += sprintf(buffer + len,
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
						len += sprintf(buffer + len,
							"%-25s      %d.%03d\n",
							sens_msg,
							pAC->I2c.SenTable[i].SenValue / 1000,
							pAC->I2c.SenTable[i].SenValue % 1000);
						break;
					case 3:
						strcpy(sens_msg, pAC->I2c.SenTable[i].SenDesc);
						strcat(sens_msg, " (rpm)");
						len += sprintf(buffer + len,
							"%-25s      %d\n",
							sens_msg,
							pAC->I2c.SenTable[i].SenValue);
						break;
					default:
						break;
					}
				}
				
				/*Receive statistics */
				len += sprintf(buffer + len, 
				"\nReceive statistics\n\n");

				len += sprintf(buffer + len,
					"Received bytes                 %Ld\n",
					(unsigned long long) pPnmiStat->StatRxOctetsOkCts);
				len += sprintf(buffer + len,
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

				len += sprintf(buffer + len,
					"Receive errors                 %Ld\n",
					(unsigned long long) pPnmiStruct->InErrorsCts);
				len += sprintf(buffer + len,
					"Receive dropped                %Ld\n",
					(unsigned long long) pPnmiStruct->RxNoBufCts);
				len += sprintf(buffer + len,
					"Received multicast             %Ld\n",
					(unsigned long long) pPnmiStat->StatRxMulticastOkCts);
				len += sprintf(buffer + len,
					"Receive error types\n");
				len += sprintf(buffer + len,
					"   length                      %Ld\n",
					(unsigned long long) pPnmiStat->StatRxRuntCts);
				len += sprintf(buffer + len,
					"   buffer overflow             %Ld\n",
					(unsigned long long) pPnmiStat->StatRxFifoOverflowCts);
				len += sprintf(buffer + len,
					"   bad crc                     %Ld\n",
					(unsigned long long) pPnmiStat->StatRxFcsCts);
				len += sprintf(buffer + len,
					"   framing                     %Ld\n",
					(unsigned long long) pPnmiStat->StatRxFramingCts);
				len += sprintf(buffer + len,
					"   missed frames               %Ld\n",
					(unsigned long long) pPnmiStat->StatRxMissedCts);

				if (pNet->Mtu > 1500)
					pPnmiStat->StatRxTooLongCts = 0;

				len += sprintf(buffer + len,
					"   too long                    %Ld\n",
					(unsigned long long) pPnmiStat->StatRxTooLongCts);					
				len += sprintf(buffer + len,
					"   carrier extension           %Ld\n",
					(unsigned long long) pPnmiStat->StatRxCextCts);				
				len += sprintf(buffer + len,
					"   too short                   %Ld\n",
					(unsigned long long) pPnmiStat->StatRxShortsCts);				
				len += sprintf(buffer + len,
					"   symbol                      %Ld\n",
					(unsigned long long) pPnmiStat->StatRxSymbolCts);				
				len += sprintf(buffer + len,
					"   LLC MAC size                %Ld\n",
					(unsigned long long) pPnmiStat->StatRxIRLengthCts);				
				len += sprintf(buffer + len,
					"   carrier event               %Ld\n",
					(unsigned long long) pPnmiStat->StatRxCarrierCts);				
				len += sprintf(buffer + len,
					"   jabber                      %Ld\n",
					(unsigned long long) pPnmiStat->StatRxJabberCts);				


				/*Transmit statistics */
				len += sprintf(buffer + len, 
				"\nTransmit statistics\n\n");
				
				len += sprintf(buffer + len,
					"Transmited bytes               %Ld\n",
					(unsigned long long) pPnmiStat->StatTxOctetsOkCts);
				len += sprintf(buffer + len,
					"Transmited packets             %Ld\n",
					(unsigned long long) pPnmiStat->StatTxOkCts);
				len += sprintf(buffer + len,
					"Transmit errors                %Ld\n",
					(unsigned long long) pPnmiStat->StatTxSingleCollisionCts);
				len += sprintf(buffer + len,
					"Transmit dropped               %Ld\n",
					(unsigned long long) pPnmiStruct->TxNoBufCts);
				len += sprintf(buffer + len,
					"Transmit collisions            %Ld\n",
					(unsigned long long) pPnmiStat->StatTxSingleCollisionCts);
				len += sprintf(buffer + len,
					"Transmit error types\n");
				len += sprintf(buffer + len,
					"   excessive collision         %ld\n",
					pAC->stats.tx_aborted_errors);
				len += sprintf(buffer + len,
					"   carrier                     %Ld\n",
					(unsigned long long) pPnmiStat->StatTxCarrierCts);
				len += sprintf(buffer + len,
					"   fifo underrun               %Ld\n",
					(unsigned long long) pPnmiStat->StatTxFifoUnderrunCts);
				len += sprintf(buffer + len,
					"   heartbeat                   %Ld\n",
					(unsigned long long) pPnmiStat->StatTxCarrierCts);
				len += sprintf(buffer + len,
					"   window                      %ld\n",
					pAC->stats.tx_window_errors);
				
			}
		}
		SkgeProcDev = next;
	}
	if (offset >= len) {
		*eof = 1;
		return 0;
	}

	*buffer_location = buffer + offset;
	if (buffer_length >= len - offset) {
		*eof = 1;
	}
	return (min_t(int, buffer_length, len - offset));
}


