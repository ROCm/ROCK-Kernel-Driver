/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *   Lowlevel functions for Hoontech STDSP24
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
#include "hoontech.h"


static void __devinit snd_ice1712_stdsp24_gpio_write(ice1712_t *ice, unsigned char byte)
{
	byte |= ICE1712_STDSP24_CLOCK_BIT;
	udelay(100);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, byte);
	byte &= ~ICE1712_STDSP24_CLOCK_BIT;
	udelay(100);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, byte);
	byte |= ICE1712_STDSP24_CLOCK_BIT;
	udelay(100);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, byte);
}

static void __devinit snd_ice1712_stdsp24_darear(ice1712_t *ice, int activate)
{
	down(&ice->gpio_mutex);
	ICE1712_STDSP24_0_DAREAR(ice->hoontech_boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[0]);
	up(&ice->gpio_mutex);
}

static void __devinit snd_ice1712_stdsp24_mute(ice1712_t *ice, int activate)
{
	down(&ice->gpio_mutex);
	ICE1712_STDSP24_3_MUTE(ice->hoontech_boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[3]);
	up(&ice->gpio_mutex);
}

static void __devinit snd_ice1712_stdsp24_insel(ice1712_t *ice, int activate)
{
	down(&ice->gpio_mutex);
	ICE1712_STDSP24_3_INSEL(ice->hoontech_boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[3]);
	up(&ice->gpio_mutex);
}

static void __devinit snd_ice1712_stdsp24_box_channel(ice1712_t *ice, int box, int chn, int activate)
{
	down(&ice->gpio_mutex);

	/* select box */
	ICE1712_STDSP24_0_BOX(ice->hoontech_boxbits, box);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[0]);

	/* prepare for write */
	if (chn == 3)
		ICE1712_STDSP24_2_CHN4(ice->hoontech_boxbits, 0);
	ICE1712_STDSP24_2_MIDI1(ice->hoontech_boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[3]);

	ICE1712_STDSP24_1_CHN1(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_1_CHN2(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_1_CHN3(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_2_CHN4(ice->hoontech_boxbits, 1);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[1]);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);
	udelay(100);
	if (chn == 3) {
		ICE1712_STDSP24_2_CHN4(ice->hoontech_boxbits, 0);
		snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);
	} else {
		switch (chn) {
		case 0:	ICE1712_STDSP24_1_CHN1(ice->hoontech_boxbits, 0); break;
		case 1:	ICE1712_STDSP24_1_CHN2(ice->hoontech_boxbits, 0); break;
		case 2:	ICE1712_STDSP24_1_CHN3(ice->hoontech_boxbits, 0); break;
		}
		snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[1]);
	}
	udelay(100);
	ICE1712_STDSP24_1_CHN1(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_1_CHN2(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_1_CHN3(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_2_CHN4(ice->hoontech_boxbits, 1);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[1]);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);
	udelay(100);

	ICE1712_STDSP24_2_MIDI1(ice->hoontech_boxbits, 0);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);

	up(&ice->gpio_mutex);
}

static void __devinit snd_ice1712_stdsp24_box_midi(ice1712_t *ice, int box, int master)
{
	down(&ice->gpio_mutex);

	/* select box */
	ICE1712_STDSP24_0_BOX(ice->hoontech_boxbits, box);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[0]);

	ICE1712_STDSP24_2_MIDIIN(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_2_MIDI1(ice->hoontech_boxbits, master);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[3]);

	udelay(100);
	
	ICE1712_STDSP24_2_MIDIIN(ice->hoontech_boxbits, 0);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);
	
	mdelay(10);
	
	ICE1712_STDSP24_2_MIDIIN(ice->hoontech_boxbits, 1);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);

	up(&ice->gpio_mutex);
}

static void __devinit snd_ice1712_stdsp24_midi2(ice1712_t *ice, int activate)
{
	down(&ice->gpio_mutex);
	ICE1712_STDSP24_3_MIDI2(ice->hoontech_boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[3]);
	up(&ice->gpio_mutex);
}

static int __devinit snd_ice1712_hoontech_init(ice1712_t *ice)
{
	int box, chn;

	ice->num_total_dacs = 8;
	ice->num_total_adcs = 8;

	ice->hoontech_boxbits[0] = 
	ice->hoontech_boxbits[1] = 
	ice->hoontech_boxbits[2] = 
	ice->hoontech_boxbits[3] = 0;	/* should be already */

	ICE1712_STDSP24_SET_ADDR(ice->hoontech_boxbits, 0);
	ICE1712_STDSP24_CLOCK(ice->hoontech_boxbits, 0, 1);
	ICE1712_STDSP24_0_BOX(ice->hoontech_boxbits, 0);
	ICE1712_STDSP24_0_DAREAR(ice->hoontech_boxbits, 0);

	ICE1712_STDSP24_SET_ADDR(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_CLOCK(ice->hoontech_boxbits, 1, 1);
	ICE1712_STDSP24_1_CHN1(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_1_CHN2(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_1_CHN3(ice->hoontech_boxbits, 1);
	
	ICE1712_STDSP24_SET_ADDR(ice->hoontech_boxbits, 2);
	ICE1712_STDSP24_CLOCK(ice->hoontech_boxbits, 2, 1);
	ICE1712_STDSP24_2_CHN4(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_2_MIDIIN(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_2_MIDI1(ice->hoontech_boxbits, 0);

	ICE1712_STDSP24_SET_ADDR(ice->hoontech_boxbits, 3);
	ICE1712_STDSP24_CLOCK(ice->hoontech_boxbits, 3, 1);
	ICE1712_STDSP24_3_MIDI2(ice->hoontech_boxbits, 0);
	ICE1712_STDSP24_3_MUTE(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_3_INSEL(ice->hoontech_boxbits, 0);

	/* let's go - activate only functions in first box */
	ice->hoontech_config = 0;
			    /* ICE1712_STDSP24_MUTE |
			       ICE1712_STDSP24_INSEL |
			       ICE1712_STDSP24_DAREAR; */
	ice->hoontech_boxconfig[0] = ICE1712_STDSP24_BOX_CHN1 |
				     ICE1712_STDSP24_BOX_CHN2 |
				     ICE1712_STDSP24_BOX_CHN3 |
				     ICE1712_STDSP24_BOX_CHN4 |
				     ICE1712_STDSP24_BOX_MIDI1 |
				     ICE1712_STDSP24_BOX_MIDI2;
	ice->hoontech_boxconfig[1] = 
	ice->hoontech_boxconfig[2] = 
	ice->hoontech_boxconfig[3] = 0;
	snd_ice1712_stdsp24_darear(ice, (ice->hoontech_config & ICE1712_STDSP24_DAREAR) ? 1 : 0);
	snd_ice1712_stdsp24_mute(ice, (ice->hoontech_config & ICE1712_STDSP24_MUTE) ? 1 : 0);
	snd_ice1712_stdsp24_insel(ice, (ice->hoontech_config & ICE1712_STDSP24_INSEL) ? 1 : 0);
	for (box = 0; box < 4; box++) {
		for (chn = 0; chn < 4; chn++)
			snd_ice1712_stdsp24_box_channel(ice, box, chn, (ice->hoontech_boxconfig[box] & (1 << chn)) ? 1 : 0);
		snd_ice1712_stdsp24_box_midi(ice, box,
				(ice->hoontech_boxconfig[box] & ICE1712_STDSP24_BOX_MIDI1) ? 1 : 0);
		if (ice->hoontech_boxconfig[box] & ICE1712_STDSP24_BOX_MIDI2)
			snd_ice1712_stdsp24_midi2(ice, 1);
	}

	return 0;
}


static int __devinit snd_ice1712_ez8_init(ice1712_t *ice)
{
	ice->gpio.write_mask = ice->eeprom.gpiomask;
	ice->gpio.direction = ice->eeprom.gpiodir;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ice->eeprom.gpiomask);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION, ice->eeprom.gpiodir);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, ice->eeprom.gpiostate);
	return 0;
}


/* entry point */
struct snd_ice1712_card_info snd_ice1712_hoontech_cards[] __devinitdata = {
	{
		.subvendor = ICE1712_SUBDEVICE_STDSP24,
		.name = "Hoontech SoundTrack Audio DSP24",
		.model = "dsp24",
		.chip_init = snd_ice1712_hoontech_init,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_STDSP24_MEDIA7_1,
		.name = "Hoontech STA DSP24 Media 7.1",
		.model = "dsp24_71",
		.chip_init = snd_ice1712_hoontech_init,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_EVENT_EZ8,	/* a dummy id */
		.name = "Event Electronics EZ8",
		.model = "ez8",
		.chip_init = snd_ice1712_ez8_init,
	},
	{ } /* terminator */
};

