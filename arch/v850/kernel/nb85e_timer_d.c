/*
 * include/asm-v850/nb85e_timer_d.c -- `Timer D' component often used
 *	with the NB85E cpu core
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/kernel.h>

#include <asm/nb85e_utils.h>
#include <asm/nb85e_timer_d.h>

/* Start interval timer TIMER (0-3).  The timer will issue the
   corresponding INTCMD interrupt RATE times per second.
   This function does not enable the interrupt.  */
void nb85e_timer_d_configure (unsigned timer, unsigned rate)
{
	unsigned divlog2, count;

	/* Calculate params for timer.  */
	if (! calc_counter_params (
		    NB85E_TIMER_D_BASE_FREQ, rate,
		    NB85E_TIMER_D_TMCD_CS_MIN, NB85E_TIMER_D_TMCD_CS_MAX, 16,
		    &divlog2, &count))
		printk (KERN_WARNING
			"Cannot find interval timer %d setting suitable"
			" for rate of %dHz.\n"
			"Using rate of %dHz instead.\n",
			timer, rate,
			(NB85E_TIMER_D_BASE_FREQ >> divlog2) >> 16);

	/* Do the actual hardware timer initialization:  */

	/* Enable timer.  */
	NB85E_TIMER_D_TMCD(timer) = NB85E_TIMER_D_TMCD_CAE;
	/* Set clock divider.  */
	NB85E_TIMER_D_TMCD(timer)
		= NB85E_TIMER_D_TMCD_CAE
		| NB85E_TIMER_D_TMCD_CS(divlog2);
	/* Set timer compare register.  */
	NB85E_TIMER_D_CMD(timer) = count;
	/* Start counting.  */
	NB85E_TIMER_D_TMCD(timer)
		= NB85E_TIMER_D_TMCD_CAE
		| NB85E_TIMER_D_TMCD_CS(divlog2)
		| NB85E_TIMER_D_TMCD_CE;
}
