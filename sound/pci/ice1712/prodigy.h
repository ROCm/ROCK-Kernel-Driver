#ifndef __SOUND_PRODIGY_H
#define __SOUND_PRODIGY_H

/*
 *   ALSA driver for VIA VT1724 (Envy24HT)
 *
 *   Lowlevel functions for Terratec PRODIGY cards
 *
 *	Copyright (c) 2003 Takashi Iwai <tiwai@suse.de>
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

#define  PRODIGY_DEVICE_DESC 	       "{AudioTrak,Prodigy 7.1},"

#define VT1724_SUBDEVICE_PRODIGY71	0x33495345	/* PRODIGY 7.1 */

extern struct snd_ice1712_card_info  snd_vt1724_prodigy_cards[];

/* GPIO bits */
#define PRODIGY_CS8415_CS	(1 << 23)
#define PRODIGY_CS8415_CDTO	(1 << 22)
#define PRODIGY_WM_RESET	(1 << 20)
#define PRODIGY_WM_CLK		(1 << 19)
#define PRODIGY_WM_DATA		(1 << 18)
#define PRODIGY_WM_RW		(1 << 17)
#define PRODIGY_AC97_RESET	(1 << 16)
#define PRODIGY_DIGITAL_SEL1	(1 << 15)
// #define PRODIGY_HP_SEL		(1 << 14)
#define PRODIGY_WM_CS		(1 << 12)

#define PRODIGY_HP_AMP_EN	(1 << 14)


/* WM8770 registers */
#define WM_DAC_ATTEN		0x00	/* DAC1-8 analog attenuation */
#define WM_DAC_MASTER_ATTEN	0x08	/* DAC master analog attenuation */
#define WM_DAC_DIG_ATTEN	0x09	/* DAC1-8 digital attenuation */
#define WM_DAC_DIG_MATER_ATTEN	0x11	/* DAC master digital attenuation */
#define WM_PHASE_SWAP		0x12	/* DAC phase */
#define WM_DAC_CTRL1		0x13	/* DAC control bits */
#define WM_MUTE			0x14	/* mute controls */
#define WM_DAC_CTRL2		0x15	/* de-emphasis and zefo-flag */
#define WM_INT_CTRL		0x16	/* interface control */
#define WM_MASTER		0x17	/* master clock and mode */
#define WM_POWERDOWN		0x18	/* power-down controls */
#define WM_ADC_GAIN		0x19	/* ADC gain L(19)/R(1a) */
#define WM_ADC_MUX		0x1b	/* input MUX */
#define WM_OUT_MUX1		0x1c	/* output MUX */
#define WM_OUT_MUX2		0x1e	/* output MUX */
#define WM_RESET		0x1f	/* software reset */


#endif /* __SOUND_PRODIGY_H */
