
/*
 **********************************************************************
 *     irqmgr.c - IRQ manager for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#include "hwaccess.h"
#include "8010.h"
#include "cardmi.h"
#include "cardmo.h"
#include "irqmgr.h"

/* Interrupt handler */

void emu10k1_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct emu10k1_card *card = (struct emu10k1_card *) dev_id;
	u32 irqstatus, tmp;

	if (!(irqstatus = emu10k1_readfn0(card, IPR)))
		return;

	DPD(4, "emu10k1_interrupt called, irq =  %u\n", irq);

	/*
	 ** NOTE :
	 ** We do a 'while loop' here cos on certain machines, with both
	 ** playback and recording going on at the same time, IRQs will
	 ** stop coming in after a while. Checking IPND indeed shows that
	 ** there are interrupts pending but the PIC says no IRQs pending.
	 ** I suspect that some boards need edge-triggered IRQs but are not
	 ** getting that condition if we don't completely clear the IPND
	 ** (make sure no more interrupts are pending).
	 ** - Eric
	 */

	do {
		DPD(4, "irq status %x\n", irqstatus);

		tmp = irqstatus;

		if (irqstatus & IRQTYPE_TIMER) {
			emu10k1_timer_irqhandler(card);
			irqstatus &= ~IRQTYPE_TIMER;
		}

		if (irqstatus & IRQTYPE_MPUIN) {
			emu10k1_mpuin_irqhandler(card);
			irqstatus &= ~IRQTYPE_MPUIN;
		}

		if (irqstatus & IRQTYPE_MPUOUT) {
			emu10k1_mpuout_irqhandler(card);
			irqstatus &= ~IRQTYPE_MPUOUT;
		}

		if (irqstatus)
			emu10k1_irq_disable(card, irqstatus);

		emu10k1_writefn0(card, IPR, tmp);

	} while ((irqstatus = emu10k1_readfn0(card, IPR)));

	return;
}

