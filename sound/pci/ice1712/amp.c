/*
 *   ALSA driver for ICEnsemble VT1724 (Envy24HT)
 *
 *   Lowlevel functions for Advanced Micro Peripherals Ltd AUDIO2000
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@suse.cz>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */      

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>

#include "ice1712.h"
#include "amp.h"


static int __devinit snd_vt1724_amp_init(ice1712_t *ice)
{
	/* only use basic functionality for now */

	ice->num_total_dacs = 2;	/* only PSDOUT0 is connected */

	return 0;
}


/* entry point */
struct snd_ice1712_card_info snd_vt1724_amp_cards[] __devinitdata = {
	{
		VT1724_SUBDEVICE_AUDIO2000,
		"AMP Ltd AUDIO2000",
		snd_vt1724_amp_init,
	},
	{ } /* terminator */
};

