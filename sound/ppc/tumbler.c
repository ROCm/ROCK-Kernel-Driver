/*
 * PMac Tumbler lowlevel functions
 *
 * Copyright (c) by Takashi Iwai <tiwai@suse.de>
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
 */


#define __NO_VERSION__
#include <sound/driver.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <sound/core.h>
#include "pmac.h"

// #define TUMBLER_TONE_CONTROL_SUPPORT

#define chip_t pmac_t

/* i2c address for tumbler */
#define TAS_I2C_ADDR	0x34

/* registers */
#define TAS_REG_MCS	0x01
#define TAS_REG_VOL	0x04
#define TAS_VOL_MAX ((1<<20) - 1)

#define TAS_REG_TREBLE	0x05
#define TAS_VOL_MAX_TREBLE	0x96	/* 1 = max, 0x96 = min */
#define TAS_REG_BASS	0x06
#define TAS_VOL_MAX_BASS	0x86	/* 1 = max, 0x86 = min */

#define TAS_MIXER_VOL_MAX	500

typedef struct pmac_tumber_t {
	pmac_keywest_t i2c;
	void *amp_mute;
	void *headphone_mute;
	void *headphone_status;
	int headphone_irq;
	int left_vol, right_vol;
	int bass_vol, treble_vol;
} pmac_tumbler_t;


/*
 * initialize / detect tumbler
 */
static int tumbler_init_client(pmac_t *chip, pmac_keywest_t *i2c)
{
	/* normal operation, SCLK=64fps, i2s output, i2s input, 16bit width */
	return snd_pmac_keywest_write_byte(i2c, TAS_REG_MCS,
					   (1<<6)+(2<<4)+(2<<2)+0);
}

/*
 * update volume
 */
static int tumbler_set_volume(pmac_tumbler_t *mix)
{
	unsigned char block[6];
	unsigned int left_vol, right_vol;
  
	if (! mix->i2c.base)
		return -ENODEV;
  
	left_vol = mix->left_vol << 6;
	right_vol = mix->right_vol << 6;

	if (left_vol > TAS_VOL_MAX)
		left_vol = TAS_VOL_MAX;
	if (right_vol > TAS_VOL_MAX)
		right_vol = TAS_VOL_MAX;
  
	block[0] = (left_vol >> 16) & 0xff;
	block[1] = (left_vol >> 8)  & 0xff;
	block[2] = (left_vol >> 0)  & 0xff;

	block[3] = (right_vol >> 16) & 0xff;
	block[4] = (right_vol >> 8)  & 0xff;
	block[5] = (right_vol >> 0)  & 0xff;
  
	if (snd_pmac_keywest_write(&mix->i2c, TAS_REG_VOL, 6, block) < 0) {
		snd_printk("failed to set volume \n");  
		return -EINVAL; 
	}
	return 0;
}


/* output volume */
static int tumbler_info_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = TAS_MIXER_VOL_MAX;
	return 0;
}

static int tumbler_get_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	if (! (mix = chip->mixer_data))
		return -ENODEV;
	ucontrol->value.integer.value[0] = mix->left_vol;
	ucontrol->value.integer.value[1] = mix->right_vol;
	return 0;
}

static int tumbler_put_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	int change;

	if (! (mix = chip->mixer_data))
		return -ENODEV;
	change = mix->left_vol != ucontrol->value.integer.value[0] ||
		mix->right_vol != ucontrol->value.integer.value[1];
	if (change) {
		mix->left_vol = ucontrol->value.integer.value[0];
		mix->right_vol = ucontrol->value.integer.value[1];
		tumbler_set_volume(mix);
	}
	return change;
}


#ifdef TUMBLER_TONE_CONTROL_SUPPORT
static int tumbler_set_bass(pmac_tumbler_t *mix)
{
	unsigned char data;
	int val;

	if (! mix->i2c.base)
		return -ENODEV;
  
	val = TAS_VOL_MAX_BASS - mix->bass_vol + 1;
	if (val < 1)
		data = 1;
	else if (val > TAS_VOL_MAX_BASS)
		data = TAS_VOL_MAX_BASS;
	else
		data = val;
	if (snd_pmac_keywest_write(&mix->i2c TAS_REG_BASS, 1, &data) < 0) {
		snd_printk("failed to set bass volume\n");  
		return -EINVAL; 
	}
	return 0;
}

static int tumbler_set_treble(pmac_tumbler_t *mix)
{
	unsigned char data;
	int val;

	if (! mix->i2c.base)
		return -ENODEV;
  
	val = TAS_VOL_MAX_TREBLE - mix->treble_vol + 1;
	if (val < 1)
		data = 1;
	else if (val > TAS_VOL_MAX_BASS)
		data = TAS_VOL_MAX_BASS;
	else
		data = val;
	if (snd_pmac_keywest_write(&mix->i2c, TAS_REG_TREBLE, 1, &data) < 0) {
		snd_printk("failed to set treble volume\n");  
		return -EINVAL; 
	}
	return 0;
}

/* bass volume */
static int tumbler_info_bass(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = TAS_VOL_MAX_BASS - 1;
	return 0;
}

static int tumbler_get_bass(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	if (! (mix = chip->mixer_data))
		return -ENODEV;
	ucontrol->value.integer.value[0] = mix->bass_vol;
	return 0;
}

static int tumbler_put_bass(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	int change;

	if (! (mix = chip->mixer_data))
		return -ENODEV;
	change = mix->bass_vol != ucontrol->value.integer.value[0];
	if (change) {
		mix->bass_vol = ucontrol->value.integer.value[0];
		tumbler_set_bass(mix);
	}
	return change;
}

static int tumbler_info_treble(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = TAS_VOL_MAX_TREBLE - 1;
	return 0;
}

static int tumbler_get_treble(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	if (! (mix = chip->mixer_data))
		return -ENODEV;
	ucontrol->value.integer.value[0] = mix->treble_vol;
	return 0;
}

static int tumbler_put_treble(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	int change;

	if (! (mix = chip->mixer_data))
		return -ENODEV;
	change = mix->treble_vol != ucontrol->value.integer.value[0];
	if (change) {
		mix->treble_vol = ucontrol->value.integer.value[0];
		tumbler_set_treble(mix);
	}
	return change;
}

#endif


static snd_kcontrol_new_t tumbler_mixers[] = {
	{ iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	  name: "Master Playback Volume",
	  info: tumbler_info_volume,
	  get: tumbler_get_volume,
	  put: tumbler_put_volume
	},
#ifdef TUMBLER_TONE_CONTROL_SUPPORT
	{ iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	  name: "Tone Control - Bass",
	  info: tumbler_info_bass,
	  get: tumbler_get_bass,
	  put: tumbler_put_bass
	},
	{ iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	  name: "Tone Control - Treble",
	  info: tumbler_info_treble,
	  get: tumbler_get_treble,
	  put: tumbler_put_treble
	},
#endif
};

#define num_controls(ary) (sizeof(ary) / sizeof(snd_kcontrol_new_t))

/* mute either amp or headphone according to the plug status */
static void tumbler_update_headphone(pmac_t *chip)
{
	pmac_tumbler_t *mix = chip->mixer_data;

	if (! mix)
		return;

	if (readb(mix->headphone_status) & 2) {
		writeb(4 + 1, mix->amp_mute);
		writeb(4 + 0, mix->headphone_mute);
	} else {
		writeb(4 + 0, mix->amp_mute);
		writeb(4 + 1, mix->headphone_mute);
	}
}

/* interrupt - headphone plug changed */
static void headphone_intr(int irq, void *devid, struct pt_regs *regs)
{
	pmac_t *chip = snd_magic_cast(pmac_t, devid, return);
	tumbler_update_headphone(chip);
}

/* look for audio-gpio device */
static struct device_node *find_audio_device(const char *name)
{
	struct device_node *np;
  
	if (! (np = find_devices("gpio")))
		return NULL;
  
	for (np = np->child; np; np = np->sibling) {
		char *property = get_property(np, "audio-gpio", NULL);
		if (property && strcmp(property, name) == 0)
			return np;
	}  
	return NULL;
}

/* find an audio device and get its address */
static unsigned long tumbler_find_device(const char *device)
{
	struct device_node *node;
	void *base;

	node = find_audio_device(device);
	if (! node) {
		snd_printd("cannot find device %s\n", device);
		return 0;
	}

	base = (void *)get_property(node, "AAPL,address", NULL);
	if (! base) {
		snd_printd("cannot find address for device %s\n", device);
		return 0;
	}

	return *(unsigned long *)base;
}

/* reset audio */
static int tumbler_reset_audio(pmac_t *chip)
{
	unsigned long base;
	void *map;

	if (! (base = tumbler_find_device("audio-hw-reset")))
		return -ENODEV;

	map = ioremap(base, 1);
	writeb(5, map);
	mdelay(100);
	writeb(4, map);
	mdelay(1);
	writeb(5, map);
	mdelay(1);
	iounmap(map);
	return 0;
}

#ifdef CONFIG_PMAC_PBOOK
/* resume mixer */
static void tumbler_resume(pmac_t *chip)
{
	pmac_tumbler_t *mix = chip->mixer_data;
	tumbler_reset_audio(chip);
	snd_pmac_keywest_write_byte(&mix->i2c, TAS_REG_MCS,
				    (1<<6)+(2<<4)+(2<<2)+0);
	tumbler_set_volume(mix);
	tumbler_update_headphone(chip); /* update mute */
}
#endif

/* initialize tumbler */
static int __init tumbler_init(pmac_t *chip)
{
	unsigned long base;
	struct device_node *node;
	int err;
	pmac_tumbler_t *mix = chip->mixer_data;

	snd_assert(mix, return -EINVAL);

	/* reset audio */
	if (tumbler_reset_audio(chip) < 0)
		return -ENODEV;

	/* get amp-mute */
	if (! (base = tumbler_find_device("amp-mute")))
		return -ENODEV;
	mix->amp_mute = ioremap(base, 1);
	if (! (base = tumbler_find_device("headphone-mute")))
		return -ENODEV;
	mix->headphone_mute = ioremap(base, 1);
	if (! (base = tumbler_find_device("headphone-detect")))
		return -ENODEV;
	mix->headphone_status = ioremap(base, 1);

	/* activate headphone status interrupts */
	writeb(readb(mix->headphone_status) | (1<<7), mix->headphone_status);

	if (! (node = find_audio_device("headphone-detect")))
		return -ENODEV;
	if (node->n_intrs == 0)
		return -ENODEV;

	if ((err = request_irq(node->intrs[0].line, headphone_intr, 0,
			       "Tumbler Headphone Detection", chip)) < 0)
		return err;
	mix->headphone_irq = node->intrs[0].line;
  
	tumbler_update_headphone(chip);
	return 0;
}

static void tumbler_cleanup(pmac_t *chip)
{
	pmac_tumbler_t *mix = chip->mixer_data;
	if (! mix)
		return;

	if (mix->headphone_irq >= 0)
		free_irq(mix->headphone_irq, chip);
	if (mix->amp_mute)
		iounmap(mix->amp_mute);
	if (mix->headphone_mute)
		iounmap(mix->headphone_mute);
	if (mix->headphone_status)
		iounmap(mix->headphone_status);
	snd_pmac_keywest_cleanup(&mix->i2c);
	kfree(mix);
	chip->mixer_data = NULL;
}

/* exported */
int __init snd_pmac_tumbler_init(pmac_t *chip)
{
	int i, err;
	pmac_tumbler_t *mix;

	mix = kmalloc(sizeof(*mix), GFP_KERNEL);
	if (! mix)
		return -ENOMEM;
	memset(mix, 0, sizeof(*mix));
	mix->headphone_irq = -1;

	chip->mixer_data = mix;
	chip->mixer_free = tumbler_cleanup;

	if ((err = tumbler_init(chip)) < 0)
		return err;

	if ((err = snd_pmac_keywest_find(chip, &mix->i2c, TAS_I2C_ADDR, tumbler_init_client)) < 0)
		return err;

	/*
	 * build mixers
	 */
	strcpy(chip->card->mixername, "PowerMac Tumbler");

	for (i = 0; i < num_controls(tumbler_mixers); i++) {
		if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&tumbler_mixers[i], chip))) < 0)
			return err;
	}

#ifdef CONFIG_PMAC_PBOOK
	chip->resume = tumbler_resume;
#endif

	return 0;
}
