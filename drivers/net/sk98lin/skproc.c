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
#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
//#define SPECIAL	32		/* 0x */
#define LARGE	64

extern char * SkNumber(char * str, long long num, int base, int size, 
				int precision ,int type);
int proc_read(char *buffer,
				char **buffer_location,
				off_t offset,
				int buffer_length,
				int *eof,
				void *data);


extern struct net_device *sk98lin_root_dev;

/*****************************************************************************
 *
 * 	proc_read - print "summaries" entry 
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
	DEV_NET				*pNet;
	SK_AC				*pAC;
	char 				test_buf[100];
	unsigned long			Flags;		
	unsigned int			Size;
	struct net_device 		*next;
	struct net_device 		*SkgeProcDev = sk98lin_root_dev;

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
					"==================================\n");
	
				/* Board statistics */
				len += sprintf(buffer + len, 
					"\nBoard statistics\n\n");
				len += sprintf(buffer + len,
					"Active Port               %c\n",
					'A' + pAC->Rlmt.Net[t-1].Port[pAC->Rlmt.
					Net[t-1].PrefPort]->PortNumber);
				len += sprintf(buffer + len,
					"Preferred Port            %c\n",
					'A' + pAC->Rlmt.Net[t-1].Port[pAC->Rlmt.
					Net[t-1].PrefPort]->PortNumber);

				len += sprintf(buffer + len,
					"Bus speed (Mhz)           %d\n",
					pPnmiStruct->BusSpeed);

				len += sprintf(buffer + len,
					"Bus width (Bit)           %d\n",
					pPnmiStruct->BusWidth);

				for (i=0; i < SK_MAX_SENSORS; i ++) {
					if (strcmp(pAC->I2c.SenTable[i].SenDesc,
							"Temperature") == 0 ) {
						len += sprintf(buffer + len,
							"Temperature (C)           %d.%d\n",
							pAC->I2c.SenTable[i].SenValue / 10,
							pAC->I2c.SenTable[i].SenValue % 10);
						len += sprintf(buffer + len,
							"Temperature (F)           %d.%d\n",
							((((pAC->I2c.SenTable[i].SenValue)
							*10)*9)/5 + 3200)/100,
							((((pAC->I2c.SenTable[i].SenValue)
							*10)*9)/5 + 3200) % 10);
					} else if (strcmp(pAC->I2c.SenTable[i].SenDesc,
							"Speed Fan") == 0 ) {
						len += sprintf(buffer + len,
							"Speed Fan                 %d\n",
							pAC->I2c.SenTable[i].SenValue);
					} else {
						len += sprintf(buffer + len,
							"%-20s      %d.%d\n",
							pAC->I2c.SenTable[i].SenDesc,
							pAC->I2c.SenTable[i].SenValue / 1000,
							pAC->I2c.SenTable[i].SenValue % 1000);
					}
				}
				
				/*Receive statistics */
				
				len += sprintf(buffer + len, 
				"\nReceive statistics\n\n");

				len += sprintf(buffer + len,
					"Received bytes            %s\n",
					SkNumber(test_buf, pPnmiStat->StatRxOctetsOkCts,
					10,0,-1,0));
				len += sprintf(buffer + len,
					"Received packets          %s\n",
					SkNumber(test_buf, pPnmiStat->StatRxOkCts,
					10,0,-1,0));
				len += sprintf(buffer + len,
					"Received errors           %s\n",
					SkNumber(test_buf, pPnmiStat->StatRxFcsCts,
					10,0,-1,0));
				len += sprintf(buffer + len,
					"Received dropped          %s\n",
					SkNumber(test_buf, pPnmiStruct->RxNoBufCts,
					10,0,-1,0));
				len += sprintf(buffer + len,
					"Received multicast        %s\n",
					SkNumber(test_buf, pPnmiStat->StatRxMulticastOkCts,
					10,0,-1,0));
				len += sprintf(buffer + len,
					"Received errors types\n");
				len += sprintf(buffer + len,
					"   length errors          %s\n",
					SkNumber(test_buf, pPnmiStat->StatRxRuntCts,
					10, 0, -1, 0));
				len += sprintf(buffer + len,
					"   over errors            %s\n",
					SkNumber(test_buf, pPnmiStat->StatRxFifoOverflowCts,
					10, 0, -1, 0));
				len += sprintf(buffer + len,
					"   crc errors             %s\n",
					SkNumber(test_buf, pPnmiStat->StatRxFcsCts,
					10, 0, -1, 0));
				len += sprintf(buffer + len,
					"   frame errors           %s\n",
					SkNumber(test_buf, pPnmiStat->StatRxFramingCts,
					10, 0, -1, 0));
				len += sprintf(buffer + len,
					"   fifo errors            %s\n",
					SkNumber(test_buf, pPnmiStat->StatRxFifoOverflowCts,
					10, 0, -1, 0));
				len += sprintf(buffer + len,
					"   missed errors          %s\n",
					SkNumber(test_buf, pPnmiStat->StatRxMissedCts,
					10, 0, -1, 0));
				
				/*Transmit statistics */
				len += sprintf(buffer + len, 
				"\nTransmit statistics\n\n");
				
				len += sprintf(buffer + len,
					"Transmit bytes            %s\n",
					SkNumber(test_buf, pPnmiStat->StatTxOctetsOkCts,
					10,0,-1,0));
				len += sprintf(buffer + len,
					"Transmit packets          %s\n",
					SkNumber(test_buf, pPnmiStat->StatTxOkCts,
					10,0,-1,0));
				len += sprintf(buffer + len,
					"Transmit errors           %s\n",
					SkNumber(test_buf, pPnmiStat->StatTxSingleCollisionCts,
					10,0,-1,0));
				len += sprintf(buffer + len,
					"Transmit dropped          %s\n",
					SkNumber(test_buf, pPnmiStruct->TxNoBufCts,
					10,0,-1,0));
				len += sprintf(buffer + len,
					"Transmit collisions       %s\n",
					SkNumber(test_buf, pPnmiStat->StatTxSingleCollisionCts,
					10,0,-1,0));
				len += sprintf(buffer + len,
					"Transmited errors types\n");
				len += sprintf(buffer + len,
					"   aborted errors         %ld\n",
					pAC->stats.tx_aborted_errors);
				len += sprintf(buffer + len,
					"   carrier errors         %s\n",
					SkNumber(test_buf, pPnmiStat->StatTxCarrierCts,
					10, 0, -1, 0));
				len += sprintf(buffer + len,
					"   fifo errors            %s\n",
					SkNumber(test_buf, pPnmiStat->StatTxFifoUnderrunCts,
					10, 0, -1, 0));
				len += sprintf(buffer + len,
					"   heartbeat errors       %s\n",
					SkNumber(test_buf, pPnmiStat->StatTxCarrierCts,
					10, 0, -1, 0));
				len += sprintf(buffer + len,
					"   window errors          %ld\n",
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





/*****************************************************************************
 *
 * SkDoDiv - convert 64bit number
 *
 * Description:
 *	This function "converts" a long long number.
 *
 * Returns:
 *	remainder of division
 */
static long SkDoDiv (long long Dividend, int Divisor, long long *pErg)
{
 long   	Rest;
 long long 	Ergebnis;
 long   	Akku;


 Akku  = Dividend >> 32;

 Ergebnis = ((long long) (Akku / Divisor)) << 32;
 Rest = Akku % Divisor ;

 Akku = Rest << 16;
 Akku |= ((Dividend & 0xFFFF0000) >> 16);


 Ergebnis += ((long long) (Akku / Divisor)) << 16;
 Rest = Akku % Divisor ;

 Akku = Rest << 16;
 Akku |= (Dividend & 0xFFFF);

 Ergebnis += (Akku / Divisor);
 Rest = Akku % Divisor ;

 *pErg = Ergebnis;
 return (Rest);
}


#if 0
#define do_div(n,base) ({ \
long long __res; \
__res = ((unsigned long long) n) % (unsigned) base; \
n = ((unsigned long long) n) / (unsigned) base; \
__res; })

#endif


/*****************************************************************************
 *
 * SkNumber - Print results
 *
 * Description:
 *	This function converts a long long number into a string.
 *
 * Returns:
 *	number as string
 */
char * SkNumber(char * str, long long num, int base, int size, int precision
	,int type)
{
	char c,sign,tmp[66], *strorg = str;
	const char *digits="0123456789abcdefghijklmnopqrstuvwxyz";
	int i;

	if (type & LARGE)
		digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	if (type & LEFT)
		type &= ~ZEROPAD;
	if (base < 2 || base > 36)
		return 0;
	c = (type & ZEROPAD) ? '0' : ' ';
	sign = 0;
	if (type & SIGN) {
		if (num < 0) {
			sign = '-';
			num = -num;
			size--;
		} else if (type & PLUS) {
			sign = '+';
			size--;
		} else if (type & SPACE) {
			sign = ' ';
			size--;
		}
	}
	if (type & SPECIAL) {
		if (base == 16)
			size -= 2;
		else if (base == 8)
			size--;
	}
	i = 0;
	if (num == 0)
		tmp[i++]='0';
	else while (num != 0)
		tmp[i++] = digits[SkDoDiv(num,base, &num)];

	if (i > precision)
		precision = i;
	size -= precision;
	if (!(type&(ZEROPAD+LEFT)))
		while(size-->0)
			*str++ = ' ';
	if (sign)
		*str++ = sign;
	if (type & SPECIAL) {
		if (base==8)
			*str++ = '0';
		else if (base==16) {
			*str++ = '0';
			*str++ = digits[33];
		}
	}
	if (!(type & LEFT))
		while (size-- > 0)
			*str++ = c;
	while (i < precision--)
		*str++ = '0';
	while (i-- > 0)
		*str++ = tmp[i];
	while (size-- > 0)
		*str++ = ' ';
	
	str[0] = '\0';
	
	return strorg;
}



