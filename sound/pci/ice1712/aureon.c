/*
 *   ALSA driver for ICEnsemble VT1724 (Envy24HT)
 *
 *   Lowlevel functions for Terratec Aureon cards
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
 *
 * NOTES:
 *
 * - we reuse the akm4xxx_t record for storing the wm8770 codec data.
 *   both wm and akm codecs are pretty similar, so we can integrate
 *   both controls in the future, once if wm codecs are reused in
 *   many boards.
 *
 * - writing over SPI is implemented but reading is not yet.
 *   the SPDIF-in channel status, etc. can be read from CS chip.
 *
 * - DAC digital volumes are not implemented in the mixer.
 *   if they show better response than DAC analog volumes, we can use them
 *   instead.
 *
 * - Aureon boards are equipped with AC97 codec, too.  it's used to do
 *   the analog mixing but not easily controllable (it's not connected
 *   directly from envy24ht chip).  so let's leave it as it is.
 *
 *
 *   Lowlevel functions for AudioTrak Prodigy 7.1 (and possibly 192) cards
 *      Copyright (c) 2003 Dimitromanolakis Apostolos <apostol@cs.utoronto.ca>
 *
 *   version 0.82: Stable / not all features work yet (no communication with AC97 secondary)
 *       added 64x/128x oversampling switch (should be 64x only for 96khz)
 *       fixed some recording labels (still need to check the rest)
 *       recording is working probably thanks to correct wm8770 initialization
 *
 *   version 0.5: Initial release:
 *           working: analog output, mixer, headphone amplifier switch
 *       not working: prety much everything else, at least i could verify that
 *                    we have no digital output, no capture, pretty bad clicks and poops
 *                    on mixer switch and other coll stuff.
 *
 * - Prodigy boards are equipped with AC97 STAC9744 chip , too.  it's used to do
 *   the analog mixing but not easily controllable (it's not connected
 *   directly from envy24ht chip).  so let's leave it as it is.
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
#include "envy24ht.h"
#include "aureon.h"

/* WM8770 registers */
#define WM_DAC_ATTEN		0x00	/* DAC1-8 analog attenuation */
#define WM_DAC_MASTER_ATTEN	0x08	/* DAC master analog attenuation */
#define WM_DAC_DIG_ATTEN	0x09	/* DAC1-8 digital attenuation */
#define WM_DAC_DIG_MASTER_ATTEN	0x11	/* DAC master digital attenuation */
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


/*
 * write data in the SPI mode
 */
static void aureon_spi_write(ice1712_t *ice, unsigned int cs, unsigned int data, int bits)
{
	unsigned int tmp;
	unsigned int cscs;
	int i;

	tmp = snd_ice1712_gpio_read(ice);

	if (ice->eeprom.subvendor == VT1724_SUBDEVICE_PRODIGY71)
		cscs = PRODIGY_CS8415_CS;
	else
		cscs = AUREON_CS8415_CS;

	snd_ice1712_gpio_set_mask(ice, ~(AUREON_WM_RW|AUREON_WM_DATA|AUREON_WM_CLK|
					 AUREON_WM_CS|cscs));
	tmp |= AUREON_WM_RW;
	tmp &= ~cs;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);

	for (i = bits - 1; i >= 0; i--) {
		tmp &= ~AUREON_WM_CLK;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
		if (data & (1 << i))
			tmp |= AUREON_WM_DATA;
		else
			tmp &= ~AUREON_WM_DATA;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
		tmp |= AUREON_WM_CLK;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
	}

	tmp &= ~AUREON_WM_CLK;
	tmp |= cs;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	tmp |= AUREON_WM_CLK;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
}
     

/*
 * get the current register value of WM codec
 */
static unsigned short wm_get(ice1712_t *ice, int reg)
{
	reg <<= 1;
	return ((unsigned short)ice->akm[0].images[reg] << 8) |
		ice->akm[0].images[reg + 1];
}

/*
 * set the register value of WM codec
 */
static void wm_put_nocache(ice1712_t *ice, int reg, unsigned short val)
{
	aureon_spi_write(ice, AUREON_WM_CS, (reg << 9) | (val & 0x1ff), 16);
}

/*
 * set the register value of WM codec and remember it
 */
static void wm_put(ice1712_t *ice, int reg, unsigned short val)
{
	wm_put_nocache(ice, reg, val);
	reg <<= 1;
	ice->akm[0].images[reg] = val >> 8;
	ice->akm[0].images[reg + 1] = val;
}

/*
 */
static int aureon_mono_bool_info(snd_kcontrol_t *k, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

/*
 * DAC mute control
 */
#define wm_dac_mute_info	aureon_mono_bool_info

static int wm_dac_mute_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	down(&ice->gpio_mutex);
	val = wm_get(ice, snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)+WM_MUTE);
	ucontrol->value.integer.value[0] = ~val>>4 & 0x1;
	up(&ice->gpio_mutex);
	return 0;
}

static int wm_dac_mute_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned short new, old;
	int change;

	snd_ice1712_save_gpio_status(ice);
	old = wm_get(ice, snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)+WM_MUTE);
	new = (~ucontrol->value.integer.value[0]<<4&0x10) | (old&~0x10);
	change = (new != old);
	if (change)
		wm_put(ice, snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)+WM_MUTE, new);
	snd_ice1712_restore_gpio_status(ice);

	return change;
}

/*
 * DAC volume attenuation mixer control
 */
static int wm_dac_vol_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	int voices = kcontrol->private_value >> 8;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = voices;
	uinfo->value.integer.min = 0;		/* mute (-101dB) */
	uinfo->value.integer.max = 101;		/* 0dB */
	return 0;
}

static int wm_dac_vol_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int i, idx, ofs, voices;
	unsigned short vol;

	voices = kcontrol->private_value >> 8;
	ofs = kcontrol->private_value & 0xff;
	down(&ice->gpio_mutex);
	for (i = 0; i < voices; i++) {
		idx  = WM_DAC_ATTEN + ofs + i;
		vol = wm_get(ice, idx) & 0x7f;
		if (vol <= 0x1a)
			ucontrol->value.integer.value[i] = 0;
		else
			ucontrol->value.integer.value[i] = vol - 0x1a;
	}
	up(&ice->gpio_mutex);
	return 0;
}

static int wm_dac_vol_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int i, idx, ofs, voices;
	unsigned short ovol, nvol;
	int change = 0;

	voices = kcontrol->private_value >> 8;
	ofs = kcontrol->private_value & 0xff;
	snd_ice1712_save_gpio_status(ice);
	for (i = 0; i < voices; i++) {
		idx  = WM_DAC_ATTEN + ofs + i;
		nvol = ucontrol->value.integer.value[i] + 0x1a;
		ovol = wm_get(ice, idx) & 0x7f;
		if (ovol != nvol) {
			if (nvol <= 0x1a && ovol <= 0x1a)
				continue;
			wm_put(ice, idx, nvol | 0x80); /* zero-detect, prelatch */
			wm_put_nocache(ice, idx, nvol | 0x180); /* update */
			change = 1;
		}
	}
	snd_ice1712_restore_gpio_status(ice);
	return change;
}

/* digital master volume */
#define MASTER_0dB 0xff
#define MASTER_RES 128	/* -64dB */
#define MASTER_MIN (MASTER_0dB - MASTER_RES)
static int wm_master_vol_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;		/* mute (-64dB) */
	uinfo->value.integer.max = MASTER_RES;	/* 0dB */
	return 0;
}

static int wm_master_vol_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	down(&ice->gpio_mutex);
	val = wm_get(ice, WM_DAC_DIG_MASTER_ATTEN) & 0xff;
	val = val > MASTER_MIN ? (val - MASTER_MIN) : 0;
	ucontrol->value.integer.value[0] = val;
	up(&ice->gpio_mutex);
	return 0;
}

static int wm_master_vol_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned short ovol, nvol;
	int change = 0;

	snd_ice1712_save_gpio_status(ice);
	nvol = ucontrol->value.integer.value[0];
	nvol = (nvol ? (nvol + MASTER_MIN) : 0) & 0xff;
	ovol = wm_get(ice, WM_DAC_DIG_MASTER_ATTEN) & 0xff;
	if (ovol != nvol) {
		wm_put(ice, WM_DAC_DIG_MASTER_ATTEN, nvol); /* prelatch */
		wm_put_nocache(ice, WM_DAC_DIG_MASTER_ATTEN, nvol | 0x100); /* update */
		change = 1;
	}
	snd_ice1712_restore_gpio_status(ice);
	return change;
}

/*
 * ADC mute control
 */
static int wm_adc_mute_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int wm_adc_mute_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	int i;

	down(&ice->gpio_mutex);
	for (i = 0; i < 2; i++) {
		val = wm_get(ice, WM_ADC_GAIN + i);
		ucontrol->value.integer.value[i] = ~val>>5 & 0x1;
	}
	up(&ice->gpio_mutex);
	return 0;
}

static int wm_adc_mute_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned short new, old;
	int i, change = 0;

	snd_ice1712_save_gpio_status(ice);
	for (i = 0; i < 2; i++) {
		old = wm_get(ice, WM_ADC_GAIN + i);
		new = (~ucontrol->value.integer.value[i]<<5&0x20) | (old&~0x20);
		if (new != old) {
			wm_put(ice, snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)+WM_ADC_GAIN, new);
			change = 1;
		}
	}
	snd_ice1712_restore_gpio_status(ice);

	return change;
}

/*
 * ADC gain mixer control
 */
static int wm_adc_vol_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;		/* -12dB */
	uinfo->value.integer.max = 0x1f;	/* 19dB */
	return 0;
}

static int wm_adc_vol_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int i, idx;
	unsigned short vol;

	down(&ice->gpio_mutex);
	for (i = 0; i < 2; i++) {
		idx = WM_ADC_GAIN + i;
		vol = wm_get(ice, idx) & 0x1f;
		ucontrol->value.integer.value[i] = vol;
	}
	up(&ice->gpio_mutex);
	return 0;
}

static int wm_adc_vol_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int i, idx;
	unsigned short ovol, nvol;
	int change = 0;

	snd_ice1712_save_gpio_status(ice);
	for (i = 0; i < 2; i++) {
		idx  = WM_ADC_GAIN + i;
		nvol = ucontrol->value.integer.value[i];
		ovol = wm_get(ice, idx);
		if ((ovol & 0x1f) != nvol) {
			wm_put(ice, idx, nvol | (ovol & ~0x1f));
			change = 1;
		}
	}
	snd_ice1712_restore_gpio_status(ice);
	return change;
}

/*
 * ADC input mux mixer control
 */
static int wm_adc_mux_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[] = {
		"CD",		//AIN1
		"Aux",		//AIN2
		"Line",		//AIN3
		"Mic",		//AIN4
		"AC97"		//AIN5
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 2;
	uinfo->value.enumerated.items = 5;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int wm_adc_mux_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	down(&ice->gpio_mutex);
	val = wm_get(ice, WM_ADC_MUX);
	ucontrol->value.integer.value[0] = val & 7;
	ucontrol->value.integer.value[1] = (val >> 4) & 7;
	up(&ice->gpio_mutex);
	return 0;
}

static int wm_adc_mux_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned short oval, nval;
	int change;

	snd_ice1712_save_gpio_status(ice);
	oval = wm_get(ice, WM_ADC_MUX);
	nval = oval & ~0x77;
	nval |= ucontrol->value.integer.value[0] & 7;
	nval |= (ucontrol->value.integer.value[1] & 7) << 4;
	change = (oval != nval);
	if (change)
		wm_put(ice, WM_ADC_MUX, nval);
	snd_ice1712_restore_gpio_status(ice);
	return 0;
}

/*
 * Headphone Amplifier
 */
static int aureon_set_headphone_amp(ice1712_t *ice, int enable)
{
	unsigned int tmp, tmp2;

	tmp2 = tmp = snd_ice1712_gpio_read(ice);
	if (enable)
		tmp |= AUREON_HP_SEL;
	else
		tmp &= ~ AUREON_HP_SEL;
	if (tmp != tmp2) {
		snd_ice1712_gpio_write(ice, tmp);
		return 1;
	}
	return 0;
}

static int aureon_get_headphone_amp(ice1712_t *ice)
{
	unsigned int tmp = snd_ice1712_gpio_read(ice);

	return ( tmp & AUREON_HP_SEL )!= 0;
}

#define aureon_hpamp_info	aureon_mono_bool_info

static int aureon_hpamp_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = aureon_get_headphone_amp(ice);
	return 0;
}


static int aureon_hpamp_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);

	return aureon_set_headphone_amp(ice,ucontrol->value.integer.value[0]);
}

/*
 * Deemphasis
 */

#define aureon_deemp_info	aureon_mono_bool_info

static int aureon_deemp_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = (wm_get(ice, WM_DAC_CTRL2) & 0xf) == 0xf;
	return 0;
}

static int aureon_deemp_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int temp, temp2;
	temp2 = temp = wm_get(ice, WM_DAC_CTRL2);
	if (ucontrol->value.integer.value[0])
		temp |= 0xf;
	else
		temp &= ~0xf;
	if (temp != temp2) {
		wm_put(ice, WM_DAC_CTRL2, temp);
		return 1;
	}
	return 0;
}

/*
 * ADC Oversampling
 */
static int aureon_oversampling_info(snd_kcontrol_t *k, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[2] = { "128x", "64x"	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;

	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);

        return 0;
}

static int aureon_oversampling_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = (wm_get(ice, WM_MASTER) & 0x8) == 0x8;
	return 0;
}

static int aureon_oversampling_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	int temp, temp2;
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);

	temp2 = temp = wm_get(ice, WM_MASTER);

	if (ucontrol->value.enumerated.item[0])
		temp |= 0x8;
	else
		temp &= ~0x8;

	if (temp != temp2) {
		wm_put(ice, WM_MASTER, temp);
		return 1;
	}
	return 0;
}

/*
 * mixers
 */

static snd_kcontrol_new_t aureon_dac_controls[] __devinitdata = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Front Playback Volume",
		.info = wm_dac_vol_info,
		.get = wm_dac_vol_get,
		.put = wm_dac_vol_put,
		.private_value = (2 << 8) | 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Rear Playback Volume",
		.info = wm_dac_vol_info,
		.get = wm_dac_vol_get,
		.put = wm_dac_vol_put,
		.private_value = (2 << 8) | 2
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Center Playback Volume",
		.info = wm_dac_vol_info,
		.get = wm_dac_vol_get,
		.put = wm_dac_vol_put,
		.private_value = (1 << 8) | 4
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "LFE Playback Volume",
		.info = wm_dac_vol_info,
		.get = wm_dac_vol_get,
		.put = wm_dac_vol_put,
		.private_value = (1 << 8) | 5
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Side Playback Volume",
		.info = wm_dac_vol_info,
		.get = wm_dac_vol_get,
		.put = wm_dac_vol_put,
		.private_value = (2 << 8) | 6
	}
};

static snd_kcontrol_new_t wm_controls[] __devinitdata = {
 	{
 		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = wm_dac_mute_info,
		.get = wm_dac_mute_get,
		.put = wm_dac_mute_put,
 	},
 	{
 		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Volume",
		.info = wm_master_vol_info,
		.get = wm_master_vol_get,
		.put = wm_master_vol_put,
 	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Switch",
		.info = wm_adc_mute_info,
		.get = wm_adc_mute_get,
		.put = wm_adc_mute_put,

	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Volume",
		.info = wm_adc_vol_info,
		.get = wm_adc_vol_get,
		.put = wm_adc_vol_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = wm_adc_mux_info,
		.get = wm_adc_mux_get,
		.put = wm_adc_mux_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Headphone Amplifier Switch",
		.info = aureon_hpamp_info,
		.get = aureon_hpamp_get,
		.put = aureon_hpamp_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DAC Deemphasis Switch",
		.info = aureon_deemp_info,
		.get = aureon_deemp_get,
		.put = aureon_deemp_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "ADC Oversampling",
		.info = aureon_oversampling_info,
		.get = aureon_oversampling_get,
		.put = aureon_oversampling_put
	},
};


static int __devinit aureon_add_controls(ice1712_t *ice)
{
	unsigned int i, counts;
	int err;

	counts = ARRAY_SIZE(aureon_dac_controls);
	if (ice->eeprom.subvendor == VT1724_SUBDEVICE_AUREON51_SKY)
		counts--; /* no side */
	for (i = 0; i < counts; i++) {
		err = snd_ctl_add(ice->card, snd_ctl_new1(&aureon_dac_controls[i], ice));
		if (err < 0)
			return err;
	}

	for (i = 0; i < ARRAY_SIZE(wm_controls); i++) {
		err = snd_ctl_add(ice->card, snd_ctl_new1(&wm_controls[i], ice));
		if (err < 0)
			return err;
	}
	return 0;
}


/*
 * initialize the chip
 */
static int __devinit aureon_init(ice1712_t *ice)
{
	static unsigned short wm_inits_aureon[] = {
		/* These come first to reduce init pop noise */
		0x1b, 0x000,		/* ADC Mux */
		0x1c, 0x009,		/* Out Mux1 */
		0x1d, 0x009,		/* Out Mux2 */

		0x18, 0x000,		/* All power-up */

		0x16, 0x122,		/* I2S, normal polarity, 24bit */
		0x17, 0x022,		/* 256fs, slave mode */
		0x00, 0,		/* DAC1 analog mute */
		0x01, 0,		/* DAC2 analog mute */
		0x02, 0,		/* DAC3 analog mute */
		0x03, 0,		/* DAC4 analog mute */
		0x04, 0,		/* DAC5 analog mute */
		0x05, 0,		/* DAC6 analog mute */
		0x06, 0,		/* DAC7 analog mute */
		0x07, 0,		/* DAC8 analog mute */
		0x08, 0x100,		/* master analog mute */
		0x09, 0xff,		/* DAC1 digital full */
		0x0a, 0xff,		/* DAC2 digital full */
		0x0b, 0xff,		/* DAC3 digital full */
		0x0c, 0xff,		/* DAC4 digital full */
		0x0d, 0xff,		/* DAC5 digital full */
		0x0e, 0xff,		/* DAC6 digital full */
		0x0f, 0xff,		/* DAC7 digital full */
		0x10, 0xff,		/* DAC8 digital full */
		0x11, 0x1ff,		/* master digital full */
		0x12, 0x000,		/* phase normal */
		0x13, 0x090,		/* unmute DAC L/R */
		0x14, 0x000,		/* all unmute */
		0x15, 0x000,		/* no deemphasis, no ZFLG */
		0x19, 0x000,		/* -12dB ADC/L */
		0x1a, 0x000,		/* -12dB ADC/R */
		(unsigned short)-1
	};
	static unsigned short wm_inits_prodigy[] = {

		/* These come first to reduce init pop noise */
		0x1b, 0x000,		/* ADC Mux */
		0x1c, 0x009,		/* Out Mux1 */
		0x1d, 0x009,		/* Out Mux2 */

		0x18, 0x000,		/* All power-up */

		0x16, 0x022,		/* I2S, normal polarity, 24bit, high-pass on */
		0x17, 0x006,		/* 128fs, slave mode */

		0x00, 0,		/* DAC1 analog mute */
		0x01, 0,		/* DAC2 analog mute */
		0x02, 0,		/* DAC3 analog mute */
		0x03, 0,		/* DAC4 analog mute */
		0x04, 0,		/* DAC5 analog mute */
		0x05, 0,		/* DAC6 analog mute */
		0x06, 0,		/* DAC7 analog mute */
		0x07, 0,		/* DAC8 analog mute */
		0x08, 0x100,		/* master analog mute */

		0x09, 0x7f,		/* DAC1 digital full */
		0x0a, 0x7f,		/* DAC2 digital full */
		0x0b, 0x7f,		/* DAC3 digital full */
		0x0c, 0x7f,		/* DAC4 digital full */
		0x0d, 0x7f,		/* DAC5 digital full */
		0x0e, 0x7f,		/* DAC6 digital full */
		0x0f, 0x7f,		/* DAC7 digital full */
		0x10, 0x7f,		/* DAC8 digital full */
		0x11, 0x1FF,		/* master digital full */

		0x12, 0x000,		/* phase normal */
		0x13, 0x090,		/* unmute DAC L/R */
		0x14, 0x000,		/* all unmute */
		0x15, 0x000,		/* no deemphasis, no ZFLG */

		0x19, 0x000,		/* -12dB ADC/L */
		0x1a, 0x000,		/* -12dB ADC/R */
		(unsigned short)-1

	};
	static unsigned short cs_inits[] = {
		0x0441, /* RUN */
		0x0100, /* no mute */
		0x0200, /* */
		0x0600, /* slave, 24bit */
		(unsigned short)-1
	};
	unsigned int tmp;
	unsigned short *p;
	unsigned int cscs;

	if (ice->eeprom.subvendor == VT1724_SUBDEVICE_AUREON51_SKY) {
		ice->num_total_dacs = 6;
		ice->num_total_adcs = 2;
	} else {
		/* aureon 7.1 and prodigy 7.1 */
		ice->num_total_dacs = 8;
		ice->num_total_adcs = 2;
	}

	/* to remeber the register values */
	ice->akm = kcalloc(1, sizeof(akm4xxx_t), GFP_KERNEL);
	if (! ice->akm)
		return -ENOMEM;
	ice->akm_codecs = 1;

	if (ice->eeprom.subvendor == VT1724_SUBDEVICE_PRODIGY71)
		cscs = PRODIGY_CS8415_CS;
	else
		cscs = AUREON_CS8415_CS;

	snd_ice1712_gpio_set_dir(ice, 0xbfffff); /* fix this for the time being */

	/* reset the wm codec as the SPI mode */
	snd_ice1712_save_gpio_status(ice);
	snd_ice1712_gpio_set_mask(ice, ~(AUREON_WM_RESET|AUREON_WM_CS|
					 cscs|AUREON_HP_SEL));
	tmp = snd_ice1712_gpio_read(ice);
	tmp &= ~AUREON_WM_RESET;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	tmp |= AUREON_WM_CS | cscs;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	tmp |= AUREON_WM_RESET;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);

	/* initialize WM8770 codec */
	if (ice->eeprom.subvendor == VT1724_SUBDEVICE_PRODIGY71)
		p = wm_inits_prodigy;
	else
		p = wm_inits_aureon;
	for (; *p != (unsigned short)-1; p += 2)
		wm_put(ice, p[0], p[1]);

	/* initialize CS8415A codec */
	for (p = cs_inits; *p != (unsigned short)-1; p++)
		aureon_spi_write(ice, cscs,
				 *p | 0x200000, 24);

	aureon_set_headphone_amp(ice, 1);

	snd_ice1712_restore_gpio_status(ice);

	return 0;
}


/*
 * Aureon boards don't provide the EEPROM data except for the vendor IDs.
 * hence the driver needs to sets up it properly.
 */

static unsigned char aureon51_eeprom[] __devinitdata = {
	0x0a,	/* SYSCONF: clock 512, spdif-in/ADC, 3DACs */
	0x80,	/* ACLINK: I2S */
	0xf8,	/* I2S: vol, 96k, 24bit, 192k */
	0xc3,	/* SPDIF: out-en, out-int, spdif-in */
	0xff,	/* GPIO_DIR */
	0xff,	/* GPIO_DIR1 */
	0xbf,	/* GPIO_DIR2 */
	0xff,	/* GPIO_MASK */
	0xff,	/* GPIO_MASK1 */
	0xff,	/* GPIO_MASK2 */
	0x00,	/* GPIO_STATE */
	0x00,	/* GPIO_STATE1 */
	0x00,	/* GPIO_STATE2 */
};

static unsigned char aureon71_eeprom[] __devinitdata = {
	0x0b,	/* SYSCONF: clock 512, spdif-in/ADC, 4DACs */
	0x80,	/* ACLINK: I2S */
	0xf8,	/* I2S: vol, 96k, 24bit, 192k */
	0xc3,	/* SPDIF: out-en, out-int, spdif-in */
	0xff,	/* GPIO_DIR */
	0xff,	/* GPIO_DIR1 */
	0xbf,	/* GPIO_DIR2 */
	0x00,	/* GPIO_MASK */
	0x00,	/* GPIO_MASK1 */
	0x00,	/* GPIO_MASK2 */
	0x00,	/* GPIO_STATE */
	0x00,	/* GPIO_STATE1 */
	0x00,	/* GPIO_STATE2 */
};

static unsigned char prodigy71_eeprom[] __devinitdata = {
	0x0b,	/* SYSCONF: clock 512, spdif-in/ADC, 4DACs */
	0x80,	/* ACLINK: I2S */
	0xf8,	/* I2S: vol, 96k, 24bit, 192k */
	0xc3,	/* SPDIF: out-en, out-int, spdif-in */
	0xff,	/* GPIO_DIR */
	0xff,	/* GPIO_DIR1 */
	0xbf,	/* GPIO_DIR2 */
	0x00,	/* GPIO_MASK */
	0x00,	/* GPIO_MASK1 */
	0x00,	/* GPIO_MASK2 */
	0x00,	/* GPIO_STATE */
	0x00,	/* GPIO_STATE1 */
	0x00,	/* GPIO_STATE2 */
};

/* entry point */
struct snd_ice1712_card_info snd_vt1724_aureon_cards[] __devinitdata = {
	{
		.subvendor = VT1724_SUBDEVICE_AUREON51_SKY,
		.name = "Terratec Aureon 5.1-Sky",
		.model = "aureon51",
		.chip_init = aureon_init,
		.build_controls = aureon_add_controls,
		.eeprom_size = sizeof(aureon51_eeprom),
		.eeprom_data = aureon51_eeprom,
		.driver = "Aureon51",
	},
	{
		.subvendor = VT1724_SUBDEVICE_AUREON71_SPACE,
		.name = "Terratec Aureon 7.1-Space",
		.model = "aureon71",
		.chip_init = aureon_init,
		.build_controls = aureon_add_controls,
		.eeprom_size = sizeof(aureon71_eeprom),
		.eeprom_data = aureon71_eeprom,
		.driver = "Aureon71",
	},
 	{
 		.subvendor = VT1724_SUBDEVICE_AUREON71_UNIVERSE,
 		.name = "Terratec Aureon 7.1-Universe",
		/* model not needed - identical with 7.1-Space */
 		.chip_init = aureon_init,
 		.build_controls = aureon_add_controls,
 		.eeprom_size = sizeof(aureon71_eeprom),
 		.eeprom_data = aureon71_eeprom,
		.driver = "Aureon71",
	},
	{
		.subvendor = VT1724_SUBDEVICE_PRODIGY71,
		.name = "Audiotrak Prodigy 7.1",
		.model = "prodigy71",
		.chip_init = aureon_init,
		.build_controls = aureon_add_controls,
		.eeprom_size = sizeof(prodigy71_eeprom),
		.eeprom_data = prodigy71_eeprom,
		.driver = "Prodigy71", /* should be identical with Aureon71 */
	},
	{ } /* terminator */
};
