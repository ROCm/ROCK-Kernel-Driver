/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *   AK4524 / AK4528 interface
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
#include <sound/core.h>
#include "ice1712.h"


/*
 * write AK4524 register
 */
void snd_ice1712_ak4524_write(ice1712_t *ice, int chip,
			      unsigned char addr, unsigned char data)
{
	unsigned char tmp, saved[2];
	int idx;
	unsigned int addrdata;
	ak4524_t *ak = &ice->ak4524;

	snd_assert(chip >= 0 && chip < 4, return);

	if (ak->ops.start) {
		if (ak->ops.start(ice, saved, chip) < 0)
			return;
	} else
		snd_ice1712_save_gpio_status(ice, saved);

	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	tmp |= ak->add_flags;
	if (ak->cif) {
		tmp |= ak->codecs_mask; /* start without chip select */
	}  else {
		tmp &= ~ak->codecs_mask; /* chip select low */
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(1);
	}

	addr &= 0x07;
	/* build I2C address + data byte */
	addrdata = 0xa000 | (addr << 8) | data;
	for (idx = 15; idx >= 0; idx--) {
		tmp &= ~(ak->data_mask | ak->clk_mask);
		if (addrdata & (1 << idx))
			tmp |= ak->data_mask;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		//udelay(200);
		udelay(1);
		tmp |= ak->clk_mask;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(1);
	}

	if ((addr != 0x04 && addr != 0x05) || (data & 0x80) == 0)
		ak->images[chip][addr] = data;
	else
		ak->ipga_gain[chip][addr-4] = data;

	if (ak->cif) {
		/* assert a cs pulse to trigger */
		tmp &= ~ak->codecs_mask;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(1);
	}
	tmp |= ak->codecs_mask; /* chip select high to trigger */
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
	udelay(1);

	if (ak->ops.stop)
		ak->ops.stop(ice, saved);
	else
		snd_ice1712_restore_gpio_status(ice, saved);
}

void snd_ice1712_ak4524_reset(ice1712_t *ice, int state)
{
	int chip;
	unsigned char reg;
	ak4524_t *ak = &ice->ak4524;
	
	for (chip = 0; chip < ak->num_dacs/2; chip++) {
		snd_ice1712_ak4524_write(ice, chip, 0x01, state ? 0x00 : 0x03);
		if (state)
			continue;
		for (reg = 0x04; reg < (ak->is_ak4528 ? 0x06 : 0x08); reg++)
			snd_ice1712_ak4524_write(ice, chip, reg, ak->images[chip][reg]);
		if (ak->is_ak4528)
			continue;
		for (reg = 0x04; reg < 0x06; reg++)
			snd_ice1712_ak4524_write(ice, chip, reg, ak->ipga_gain[chip][reg-4]);
	}
}

/*
 * initialize all the ak4524/4528 chips
 */
void __devinit snd_ice1712_ak4524_init(ice1712_t *ice)
{
	static unsigned char inits[] = {
		0x00, 0x07, /* 0: all power up */
		0x01, 0x00, /* 1: ADC/DAC reset */
		0x02, 0x60, /* 2: 24bit I2S */
		0x03, 0x19, /* 3: deemphasis off */
		0x01, 0x03, /* 1: ADC/DAC enable */
		0x04, 0x00, /* 4: ADC left muted */
		0x05, 0x00, /* 5: ADC right muted */
		0x04, 0x80, /* 4: ADC IPGA gain 0dB */
		0x05, 0x80, /* 5: ADC IPGA gain 0dB */
		0x06, 0x00, /* 6: DAC left muted */
		0x07, 0x00, /* 7: DAC right muted */
		0xff, 0xff
	};
	int chip, idx;
	unsigned char *ptr, reg, data;
	ak4524_t *ak = &ice->ak4524;

	for (chip = idx = 0; chip < ak->num_dacs/2; chip++) {
		ptr = inits;
		while (*ptr != 0xff) {
			reg = *ptr++;
			data = *ptr++;
			if (ak->is_ak4528) {
				if (reg > 5)
					continue;
				if (reg >= 4 && (data & 0x80))
					continue;
			}
			if (reg == 0x03 && ak->is_ak4528)
				data = 0x0d;	/* deemphasis off, turn LR highpass filters on */
			snd_ice1712_ak4524_write(ice, chip, reg, data);
		}
	}
}

static int snd_ice1712_ak4524_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 127;
	return 0;
}

static int snd_ice1712_ak4524_volume_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int chip = kcontrol->private_value / 8;
	int addr = kcontrol->private_value % 8;
	ucontrol->value.integer.value[0] = ice->ak4524.images[chip][addr];
	return 0;
}

static int snd_ice1712_ak4524_volume_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int chip = kcontrol->private_value / 8;
	int addr = kcontrol->private_value % 8;
	unsigned char nval = ucontrol->value.integer.value[0];
	int change = ice->ak4524.images[chip][addr] != nval;
	if (change)
		snd_ice1712_ak4524_write(ice, chip, addr, nval);
	return change;
}

static int snd_ice1712_ak4524_ipga_gain_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 36;
	return 0;
}

static int snd_ice1712_ak4524_ipga_gain_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int chip = kcontrol->private_value / 8;
	int addr = kcontrol->private_value % 8;
	ucontrol->value.integer.value[0] = ice->ak4524.ipga_gain[chip][addr-4] & 0x7f;
	return 0;
}

static int snd_ice1712_ak4524_ipga_gain_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int chip = kcontrol->private_value / 8;
	int addr = kcontrol->private_value % 8;
	unsigned char nval = (ucontrol->value.integer.value[0] % 37) | 0x80;
	int change = ice->ak4524.ipga_gain[chip][addr] != nval;
	if (change)
		snd_ice1712_ak4524_write(ice, chip, addr, nval);
	return change;
}

static int snd_ice1712_ak4524_deemphasis_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[4] = {
		"44.1kHz", "Off", "48kHz", "32kHz",
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item >= 4)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ice1712_ak4524_deemphasis_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int chip = kcontrol->id.index;
	ucontrol->value.enumerated.item[0] = ice->ak4524.images[chip][3] & 3;
	return 0;
}

static int snd_ice1712_ak4524_deemphasis_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int chip = kcontrol->id.index;
	unsigned char nval = ucontrol->value.enumerated.item[0];
	int change;
	nval |= (nval & 3) | (ice->ak4524.images[chip][3] & ~3);
	change = ice->ak4524.images[chip][3] != nval;
	if (change)
		snd_ice1712_ak4524_write(ice, chip, 3, nval);
	return change;
}

/*
 * build AK4524 controls
 */

int __devinit snd_ice1712_ak4524_build_controls(ice1712_t *ice)
{
	int err, idx;
	ak4524_t *ak = &ice->ak4524;

	for (idx = 0; idx < ak->num_dacs; ++idx) {
		snd_kcontrol_t ctl;
		memset(&ctl, 0, sizeof(ctl));
		strcpy(ctl.id.name, "DAC Volume");
		ctl.id.index = idx;
		ctl.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		ctl.info = snd_ice1712_ak4524_volume_info;
		ctl.access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE;
		ctl.get = snd_ice1712_ak4524_volume_get;
		ctl.put = snd_ice1712_ak4524_volume_put;
		if (ak->is_ak4528)
			ctl.private_value = (idx / 2) * 8 + (idx % 2) + 4; /* register 4 & 5 */
		else
			ctl.private_value = (idx / 2) * 8 + (idx % 2) + 6; /* register 6 & 7 */
		ctl.private_data = ice;
		if ((err = snd_ctl_add(ice->card, snd_ctl_new(&ctl))) < 0)
			return err;
	}
	for (idx = 0; idx < ak->num_adcs && !ak->is_ak4528; ++idx) {
		snd_kcontrol_t ctl;
		memset(&ctl, 0, sizeof(ctl));
		strcpy(ctl.id.name, "ADC Volume");
		ctl.id.index = idx;
		ctl.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		ctl.info = snd_ice1712_ak4524_volume_info;
		ctl.access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE;
		ctl.get = snd_ice1712_ak4524_volume_get;
		ctl.put = snd_ice1712_ak4524_volume_put;
		ctl.private_value = (idx / 2) * 8 + (idx % 2) + 4; /* register 4 & 5 */
		ctl.private_data = ice;
		if ((err = snd_ctl_add(ice->card, snd_ctl_new(&ctl))) < 0)
			return err;
		memset(&ctl, 0, sizeof(ctl));
		strcpy(ctl.id.name, "IPGA Analog Capture Volume");
		ctl.id.index = idx;
		ctl.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		ctl.info = snd_ice1712_ak4524_ipga_gain_info;
		ctl.access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE;
		ctl.get = snd_ice1712_ak4524_ipga_gain_get;
		ctl.put = snd_ice1712_ak4524_ipga_gain_put;
		ctl.private_value = (idx / 2) * 8 + (idx % 2) + 4; /* register 4 & 5 */
		ctl.private_data = ice;
		if ((err = snd_ctl_add(ice->card, snd_ctl_new(&ctl))) < 0)
			return err;
	}
	for (idx = 0; idx < ak->num_dacs/2; idx++) {
		snd_kcontrol_t ctl;
		memset(&ctl, 0, sizeof(ctl));
		strcpy(ctl.id.name, "Deemphasis");
		ctl.id.index = idx;
		ctl.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		ctl.info = snd_ice1712_ak4524_deemphasis_info;
		ctl.access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE;
		ctl.get = snd_ice1712_ak4524_deemphasis_get;
		ctl.put = snd_ice1712_ak4524_deemphasis_put;
		ctl.private_data = ice;
		if ((err = snd_ctl_add(ice->card, snd_ctl_new(&ctl))) < 0)
			return err;
	}
	return 0;
}


