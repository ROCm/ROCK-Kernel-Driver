/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *   Lowlevel functions for M-Audio Delta 1010, 44, 66, Dio2496, Audiophile
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
#include <sound/cs8427.h>
#include <sound/asoundef.h>

#include "ice1712.h"
#include "delta.h"

#define SND_CS8403
#include <sound/cs8403.h>


/*
 * CS8427 via SPI mode (for Audiophile), emulated I2C
 */

/* send 8 bits */
static void ap_cs8427_write_byte(ice1712_t *ice, unsigned char data, unsigned char tmp)
{
	int idx;

	for (idx = 7; idx >= 0; idx--) {
		tmp &= ~(ICE1712_DELTA_AP_DOUT|ICE1712_DELTA_AP_CCLK);
		if (data & (1 << idx))
			tmp |= ICE1712_DELTA_AP_DOUT;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(5);
		tmp |= ICE1712_DELTA_AP_CCLK;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(5);
	}
}

/* read 8 bits */
static unsigned char ap_cs8427_read_byte(ice1712_t *ice, unsigned char tmp)
{
	unsigned char data = 0;
	int idx;
	
	for (idx = 7; idx >= 0; idx--) {
		tmp &= ~ICE1712_DELTA_AP_CCLK;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(5);
		if (snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA) & ICE1712_DELTA_AP_DIN)
			data |= 1 << idx;
		tmp |= ICE1712_DELTA_AP_CCLK;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(5);
	}
	return data;
}

/* assert chip select */
static unsigned char ap_cs8427_codec_select(ice1712_t *ice)
{
	unsigned char tmp;
	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	tmp |= ICE1712_DELTA_AP_CCLK | ICE1712_DELTA_AP_CS_CODEC;
	tmp &= ~ICE1712_DELTA_AP_CS_DIGITAL;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
	udelay(5);
	return tmp;
}

/* deassert chip select */
static void ap_cs8427_codec_deassert(ice1712_t *ice, unsigned char tmp)
{
	tmp |= ICE1712_DELTA_AP_CS_DIGITAL;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
}

/* sequential write */
static int ap_cs8427_sendbytes(snd_i2c_device_t *device, unsigned char *bytes, int count)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, device->bus->private_data, return -EIO);
	int res = count;
	unsigned char tmp;

	down(&ice->gpio_mutex);
	tmp = ap_cs8427_codec_select(ice);
	ap_cs8427_write_byte(ice, (device->addr << 1) | 0, tmp); /* address + write mode */
	while (count-- > 0)
		ap_cs8427_write_byte(ice, *bytes++, tmp);
	ap_cs8427_codec_deassert(ice, tmp);
	up(&ice->gpio_mutex);
	return res;
}

/* sequential read */
static int ap_cs8427_readbytes(snd_i2c_device_t *device, unsigned char *bytes, int count)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, device->bus->private_data, return -EIO);
	int res = count;
	unsigned char tmp;
	
	down(&ice->gpio_mutex);
	tmp = ap_cs8427_codec_select(ice);
	ap_cs8427_write_byte(ice, (device->addr << 1) | 1, tmp); /* address + read mode */
	while (count-- > 0)
		*bytes++ = ap_cs8427_read_byte(ice, tmp);
	ap_cs8427_codec_deassert(ice, tmp);
	up(&ice->gpio_mutex);
	return res;
}

static int ap_cs8427_probeaddr(snd_i2c_bus_t *bus, unsigned short addr)
{
	if (addr == 0x10)
		return 1;
	return -ENOENT;
}

static snd_i2c_ops_t ap_cs8427_i2c_ops = {
	.sendbytes = ap_cs8427_sendbytes,
	.readbytes = ap_cs8427_readbytes,
	.probeaddr = ap_cs8427_probeaddr,
};

/*
 */

static void snd_ice1712_delta_cs8403_spdif_write(ice1712_t *ice, unsigned char bits)
{
	unsigned char tmp, mask1, mask2;
	int idx;
	/* send byte to transmitter */
	mask1 = ICE1712_DELTA_SPDIF_OUT_STAT_CLOCK;
	mask2 = ICE1712_DELTA_SPDIF_OUT_STAT_DATA;
	down(&ice->gpio_mutex);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	for (idx = 7; idx >= 0; idx--) {
		tmp &= ~(mask1 | mask2);
		if (bits & (1 << idx))
			tmp |= mask2;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(100);
		tmp |= mask1;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(100);
	}
	tmp &= ~mask1;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
	up(&ice->gpio_mutex);
}


static void delta_spdif_default_get(ice1712_t *ice, snd_ctl_elem_value_t * ucontrol)
{
	snd_cs8403_decode_spdif_bits(&ucontrol->value.iec958, ice->spdif.cs8403_bits);
}

static int delta_spdif_default_put(ice1712_t *ice, snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	unsigned int val;
	int change;

	val = snd_cs8403_encode_spdif_bits(&ucontrol->value.iec958);
	spin_lock_irqsave(&ice->reg_lock, flags);
	change = ice->spdif.cs8403_bits != val;
	ice->spdif.cs8403_bits = val;
	if (change && ice->playback_pro_substream == NULL) {
		spin_unlock_irqrestore(&ice->reg_lock, flags);
		snd_ice1712_delta_cs8403_spdif_write(ice, val);
	} else {
		spin_unlock_irqrestore(&ice->reg_lock, flags);
	}
	return change;
}

static void delta_spdif_stream_get(ice1712_t *ice, snd_ctl_elem_value_t * ucontrol)
{
	snd_cs8403_decode_spdif_bits(&ucontrol->value.iec958, ice->spdif.cs8403_stream_bits);
}

static int delta_spdif_stream_put(ice1712_t *ice, snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	unsigned int val;
	int change;

	val = snd_cs8403_encode_spdif_bits(&ucontrol->value.iec958);
	spin_lock_irqsave(&ice->reg_lock, flags);
	change = ice->spdif.cs8403_stream_bits != val;
	ice->spdif.cs8403_stream_bits = val;
	if (change && ice->playback_pro_substream != NULL) {
		spin_unlock_irqrestore(&ice->reg_lock, flags);
		snd_ice1712_delta_cs8403_spdif_write(ice, val);
	} else {
		spin_unlock_irqrestore(&ice->reg_lock, flags);
	}
	return change;
}


/*
 * AK4524 on Delta 44 and 66 to choose the chip mask
 */
static int delta_ak4524_start(ice1712_t *ice, unsigned char *saved, int chip)
{
	snd_ice1712_save_gpio_status(ice, saved);
	ice->ak4524.codecs_mask = chip == 0 ? ICE1712_DELTA_CODEC_CHIP_A : ICE1712_DELTA_CODEC_CHIP_B;
	return 0;
}

/*
 * change the rate of AK4524 on Delta 44/66 and AP
 */
static void delta_ak4524_set_rate_val(ice1712_t *ice, unsigned char val)
{
	unsigned char tmp, tmp2;

	/* check before reset ak4524 to avoid unnecessary clicks */
	down(&ice->gpio_mutex);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	up(&ice->gpio_mutex);
	tmp2 = tmp;
	tmp2 &= ~ICE1712_DELTA_DFS; 
	if (val == 15 || val == 11 || val == 7)
		tmp2 |= ICE1712_DELTA_DFS;
	if (tmp == tmp2)
		return;

	/* do it again */
	snd_ice1712_ak4524_reset(ice, 1);
	down(&ice->gpio_mutex);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	if (val == 15 || val == 11 || val == 7) {
		tmp |= ICE1712_DELTA_DFS;
	} else {
		tmp &= ~ICE1712_DELTA_DFS;
	}
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
	up(&ice->gpio_mutex);
	snd_ice1712_ak4524_reset(ice, 0);
}


/*
 * SPDIF ops for Delta 1010, Dio, 66
 */

/* open callback */
static void delta_open_spdif(ice1712_t *ice, snd_pcm_substream_t * substream)
{
	ice->spdif.cs8403_stream_bits = ice->spdif.cs8403_bits;
}

/* set up */
static void delta_setup_spdif(ice1712_t *ice, snd_pcm_substream_t * substream)
{
	unsigned long flags;
	unsigned int tmp;
	int change;

	spin_lock_irqsave(&ice->reg_lock, flags);
	tmp = ice->spdif.cs8403_stream_bits;
	if (tmp & 0x01)		/* consumer */
		tmp &= (tmp & 0x01) ? ~0x06 : ~0x18;
	switch (substream->runtime->rate) {
	case 32000: tmp |= (tmp & 0x01) ? 0x04 : 0x00; break;
	case 44100: tmp |= (tmp & 0x01) ? 0x00 : 0x10; break;
	case 48000: tmp |= (tmp & 0x01) ? 0x02 : 0x08; break;
	default: tmp |= (tmp & 0x01) ? 0x00 : 0x18; break;
	}
	change = ice->spdif.cs8403_stream_bits != tmp;
	ice->spdif.cs8403_stream_bits = tmp;
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	if (change)
		snd_ctl_notify(ice->card, SNDRV_CTL_EVENT_MASK_VALUE, &ice->spdif.stream_ctl->id);
	snd_ice1712_delta_cs8403_spdif_write(ice, tmp);
}


/*
 * initialize the chips on M-Audio cards
 */

static int __devinit snd_ice1712_delta_init(ice1712_t *ice)
{
	int err;
	ak4524_t *ak;

	/* determine I2C, DACs and ADCs */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_AUDIOPHILE:
		ice->num_total_dacs = 2;
		break;
	case ICE1712_SUBDEVICE_DELTA44:
	case ICE1712_SUBDEVICE_DELTA66:
		ice->num_total_dacs = ice->omni ? 8 : 4;
		break;
	case ICE1712_SUBDEVICE_DELTA1010:
		ice->num_total_dacs = 8;
		break;
	}

	/* initialize spdif */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_AUDIOPHILE:
		if ((err = snd_i2c_bus_create(ice->card, "ICE1712 GPIO 1", NULL, &ice->i2c)) < 0) {
			snd_printk("unable to create I2C bus\n");
			return err;
		}
		ice->i2c->private_data = ice;
		ice->i2c->ops = &ap_cs8427_i2c_ops;
		if ((err = snd_ice1712_init_cs8427(ice, CS8427_BASE_ADDR)) < 0)
			return err;
		break;
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
		ice->spdif.ops.open = delta_open_spdif;
		ice->spdif.ops.setup = delta_setup_spdif;
		ice->spdif.ops.default_get = delta_spdif_default_get;
		ice->spdif.ops.default_put = delta_spdif_default_put;
		ice->spdif.ops.stream_get = delta_spdif_stream_get;
		ice->spdif.ops.stream_put = delta_spdif_stream_put;
		/* Set spdif defaults */
		snd_ice1712_delta_cs8403_spdif_write(ice, ice->spdif.cs8403_bits);
		break;
	}

	/* second stage of initialization, analog parts and others */
	ak = &ice->ak4524;
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_AUDIOPHILE:
		ak->num_adcs = ak->num_dacs = 2;
		ak->is_ak4528 = 1;
		ak->cif = 0; /* the default level of the CIF pin from AK4524 */
		ak->data_mask = ICE1712_DELTA_AP_DOUT;
		ak->clk_mask = ICE1712_DELTA_AP_CCLK;
		ak->codecs_mask = ICE1712_DELTA_AP_CS_CODEC; /* select AK4528 codec */
		ak->add_flags = ICE1712_DELTA_AP_CS_DIGITAL; /* assert digital high */
		ak->ops.set_rate_val = delta_ak4524_set_rate_val;
		snd_ice1712_ak4524_init(ice);
		break;
	case ICE1712_SUBDEVICE_DELTA66:
	case ICE1712_SUBDEVICE_DELTA44:
		ak->num_adcs = ak->num_dacs = 4;
		ak->cif = 0; /* the default level of the CIF pin from AK4524 */
		ak->data_mask = ICE1712_DELTA_CODEC_SERIAL_DATA;
		ak->clk_mask = ICE1712_DELTA_CODEC_SERIAL_CLOCK;
		ak->codecs_mask = 0; /* set later */
		ak->add_flags = 0;
		ak->ops.start = delta_ak4524_start;
		ak->ops.set_rate_val = delta_ak4524_set_rate_val;
		snd_ice1712_ak4524_init(ice);
		break;
	}

	return 0;
}


/*
 * additional controls for M-Audio cards
 */

static snd_kcontrol_new_t snd_ice1712_delta1010_wordclock_select __devinitdata =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_PCM, "Word Clock Sync", 0, ICE1712_DELTA_WORD_CLOCK_SELECT, 1, 0);
static snd_kcontrol_new_t snd_ice1712_delta1010_wordclock_status __devinitdata =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_PCM, "Word Clock Status", 0, ICE1712_DELTA_WORD_CLOCK_STATUS, 1, SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE);
static snd_kcontrol_new_t snd_ice1712_deltadio2496_spdif_in_select __devinitdata =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_PCM, "IEC958 Input Optical", 0, ICE1712_DELTA_SPDIF_INPUT_SELECT, 0, 0);
static snd_kcontrol_new_t snd_ice1712_delta_spdif_in_status __devinitdata =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_PCM, "Delta IEC958 Input Status", 0, ICE1712_DELTA_SPDIF_IN_STAT, 1, SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE);


static int __devinit snd_ice1712_delta_add_controls(ice1712_t *ice)
{
	int err;

	/* 1010 and dio specific controls */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_delta1010_wordclock_select, ice));
		if (err < 0)
			return err;
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_delta1010_wordclock_status, ice));
		if (err < 0)
			return err;
		break;
	case ICE1712_SUBDEVICE_DELTADIO2496:
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_deltadio2496_spdif_in_select, ice));
		if (err < 0)
			return err;
		break;
	}

	/* normal spdif controls */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
	case ICE1712_SUBDEVICE_AUDIOPHILE:
		err = snd_ice1712_spdif_build_controls(ice);
		if (err < 0)
			return err;
		break;
	}

	/* spdif status in */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_delta_spdif_in_status, ice));
		if (err < 0)
			return err;
		break;
	}

	/* ak4524 controls */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_DELTA44:
	case ICE1712_SUBDEVICE_DELTA66:
		err = snd_ice1712_ak4524_build_controls(ice);
		if (err < 0)
			return err;
		break;
	}
	return 0;
}


/* entry point */
struct snd_ice1712_card_info snd_ice1712_delta_cards[] __devinitdata = {
	{
		ICE1712_SUBDEVICE_DELTA1010,
		"M Audio Delta 1010",
		snd_ice1712_delta_init,
		snd_ice1712_delta_add_controls,
	},
	{
		ICE1712_SUBDEVICE_DELTADIO2496,
		"M Audio Delta DiO 2496",
		snd_ice1712_delta_init,
		snd_ice1712_delta_add_controls,
		1, /* NO MPU */
	},
	{
		ICE1712_SUBDEVICE_DELTA66,
		"M Audio Delta 66",
		snd_ice1712_delta_init,
		snd_ice1712_delta_add_controls,
		1, /* NO MPU */
	},
	{
		ICE1712_SUBDEVICE_DELTA44,
		"M Audio Delta 44",
		snd_ice1712_delta_init,
		snd_ice1712_delta_add_controls,
		1, /* NO MPU */
	},
	{
		ICE1712_SUBDEVICE_AUDIOPHILE,
		"M Audio Audiophile 24/96",
		snd_ice1712_delta_init,
		snd_ice1712_delta_add_controls,
	},
	{
		ICE1712_SUBDEVICE_DELTA1010LT,
		"M Audio Delta 1010LT",
		snd_ice1712_delta_init,
		snd_ice1712_delta_add_controls,
	},
	{ } /* terminator */
};
