
/*
 *
 * Copyright (C) Eicon Technology Corporation, 2000.
 *
 * This source file is supplied for the exclusive use with Eicon
 * Technology Corporation's range of DIVA Server Adapters.
 *
 * Eicon File Revision :    1.10  
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY 
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


#include <linux/sched.h>
#undef N_DATA
#include <linux/tqueue.h>

#include <linux/smp.h>
struct pt_regs;
#include <linux/interrupt.h>
#include <linux/ioport.h>

#include "sys.h"
#include "divas.h"
#include "adapter.h"
#include "divalog.h"

#include "uxio.h"

#ifdef MODULE
void bcopy(void *pSource, void *pDest, dword dwLength)
{
	memcpy(pDest, pSource, dwLength);
}
#endif

void bzero(void *pDataArea, dword dwLength)
{
	memset(pDataArea, 0, dwLength);
}

int Divas4BRIInitPCI(card_t *card, dia_card_t *cfg)
{
	/* Use UxPciConfigWrite	routines to initialise PCI config space */

/*	wPCIcommand = 0x03;
	cm_write_devconfig16(CMKey, PCI_COMMAND, &wPCIcommand);

	wPCIcommand = 0x280;
	cm_write_devconfig16(CMKey, PCI_STATUS, &wPCIcommand);

	bPCIcommand = 0x30;
	cm_write_devconfig16(CMKey, PCI_STATUS, &wPCIcommand);
*/
	return 0; 
}

int DivasPRIInitPCI(card_t *card, dia_card_t *cfg)
{
	/* Use UxPciConfigWrite	routines to initialise PCI config space */

/*		wPCIcommand = 0x03;
	cm_write_devconfig16(CMKey, PCI_COMMAND, &wPCIcommand);
	
	wPCIcommand = 0x280;
	cm_write_devconfig16(CMKey, PCI_STATUS, &wPCIcommand);
	
	bPCIcommand = 0x30;
	cm_write_devconfig8(CMKey, PCI_LATENCY, &bPCIcommand);*/

	return 0;  
}

int DivasBRIInitPCI(card_t *card, dia_card_t *cfg)
{
	/* Need to set these platform dependent values after patching */

	card->hw->reset_base = card->cfg.reset_base;
	card->hw->io_base = card->cfg.io_base;

	request_region(card->hw->reset_base,0x80,"Divas");
	request_region(card->hw->io_base,0x20,"Divas");


	/* Same as for PRI */
	return DivasPRIInitPCI(card, cfg);
}

/* ######################### Stubs of routines that are not done yet ################## */
/*void DivasLogIdi(card_t *card, ENTITY *e, int request)
{
}
*/

int	DivasDpcSchedule(void)
{
	static	struct tq_struct DivasTask;

	DivasTask.routine = DivasDoDpc;
	DivasTask.data = (void *) 0;

	queue_task(&DivasTask, &tq_immediate);
	mark_bh(IMMEDIATE_BH);

	return 0;
}

int	DivasScheduleRequestDpc(void)
{
	static	struct tq_struct DivasTask;

	DivasTask.routine = DivasDoRequestDpc;
	DivasTask.data = (void *) 0;

	queue_task(&DivasTask, &tq_immediate);
	mark_bh(IMMEDIATE_BH);

	return 0;
}

void    DivasLogAdd(void *buffer, int length)
{
    static
    boolean_t   overflow = FALSE;
    static
    boolean_t   busy = FALSE;

    /* make sure we're not interrupting ourselves */

    if (busy)
    {
        printk(KERN_DEBUG "Divas: Logging interrupting self !\n");
        return;
    }
    busy = TRUE;

    /* ignore call if daemon isn't running and we've reached limit */

    if (DivasLogFifoFull())
    {
        if (!overflow)
        {
            printk(KERN_DEBUG "Divas: Trace buffer full\n");
            overflow = TRUE;
        }
        busy = FALSE;
        return;
    }

	DivasLogFifoWrite(buffer, length);

    busy = FALSE;
    return;
}

/* #################################################################################### */
