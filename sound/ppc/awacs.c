/*
 * PMac AWACS lowlevel functions
 *
 * Copyright (c) by Takashi Iwai <tiwai@suse.de>
 * code based on dmasound.c.
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
#include <asm/io.h>
#include <linux/init.h>
#include <sound/core.h>
#include "pmac.h"

#define chip_t pmac_t


/*
 * write AWACS register
 */
static void
snd_pmac_awacs_write(pmac_t *chip, int val)
{
	long timeout = 5000000;

	if (chip->model <= PMAC_SCREAMER)
		return;

	while (in_le32(&chip->awacs->codec_ctrl) & MASK_NEWECMD) {
		if (! --timeout) {
			snd_printd("snd_pmac_awacs_write timeout\n");
			break;
		}
	}
	out_le32(&chip->awacs->codec_ctrl, val | (chip->subframe << 22));
}

static void
snd_pmac_awacs_write_reg(pmac_t *chip, int reg, int val)
{
	snd_pmac_awacs_write(chip, val | (reg << 12));
	chip->awacs_reg[reg] = val;
}

static void
snd_pmac_awacs_write_noreg(pmac_t *chip, int reg, int val)
{
	snd_pmac_awacs_write(chip, val | (reg << 12));
}

#ifdef CONFIG_PMAC_PBOOK
static void
screamer_recalibrate(pmac_t *chip)
{
	/* Sorry for the horrible delays... I hope to get that improved
	 * by making the whole PM process asynchronous in a future version
	 */
	mdelay(750);
	snd_pmac_awacs_write_noreg(chip, 1,
				   chip->awacs_reg[1] | MASK_RECALIBRATE | MASK_CMUTE | MASK_AMUTE);
	mdelay(1000);
	snd_pmac_awacs_write_noreg(chip, 1, chip->awacs_reg[1]);
}
#endif


/*
 * additional callback to set the pcm format
 */
static void snd_pmac_awacs_set_format(pmac_t *chip)
{
	chip->awacs_reg[1] &= ~MASK_SAMPLERATE;
	chip->awacs_reg[1] |= chip->rate_index << 3;
	snd_pmac_awacs_write_reg(chip, 1, chip->awacs_reg[1]);
}


#ifdef PMAC_AMP_AVAIL
/* Turn on sound output, needed on G3 desktop powermacs */
/* vol = 0 - 31, stereo */
static void
snd_pmac_awacs_enable_amp(pmac_t *chip, int lvol, int rvol)
{
	struct adb_request req;

	if (! chip->amp_only)
		return;

	/* turn on headphones */
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
		     0x8a, 4, 0);
	while (!req.complete) cuda_poll();
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
		     0x8a, 6, 0);
	while (!req.complete) cuda_poll();

	/* turn on speaker */
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
		     0x8a, 3, lvol & 0xff);
	while (!req.complete) cuda_poll();
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
		     0x8a, 5, rvol & 0xff);
	while (!req.complete) cuda_poll();

	cuda_request(&req, NULL, 5, CUDA_PACKET,
		     CUDA_GET_SET_IIC, 0x8a, 1, 0x29);
	while (!req.complete) cuda_poll();

	/* update */
	chip->amp_vol[0] = lvol;
	chip->amp_vol[1] = rvol;
}
#endif

/*
 * AWACS volume callbacks
 */
/*
 * volumes: 0-15 stereo
 */
static int snd_pmac_awacs_info_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 15;
	return 0;
}
 
static int snd_pmac_awacs_get_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	int n = kcontrol->private_value & 0xff;
	int lshift = (kcontrol->private_value >> 8) & 0xff;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	ucontrol->value.integer.value[0] = 0x0f - ((chip->awacs_reg[n] >> lshift) & 0xf);
	ucontrol->value.integer.value[1] = 0x0f - (chip->awacs_reg[n] & 0xf);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_pmac_awacs_put_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	int n = kcontrol->private_value & 0xff;
	int lshift = (kcontrol->private_value >> 8) & 0xff;
	int rn, oldval;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	oldval = chip->awacs_reg[n];
	rn = oldval & ~(0xf | (0xf << lshift));
	rn |= ((0x0f - (ucontrol->value.integer.value[0] & 0xf)) << lshift);
	rn |= 0x0f - (ucontrol->value.integer.value[1] & 0xf);
	snd_pmac_awacs_write_reg(chip, n, rn);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return oldval != rn;
}


#define AWACS_VOLUME(xname, xreg, xshift) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, index: 0, \
  info: snd_pmac_awacs_info_volume, \
  get: snd_pmac_awacs_get_volume, \
  put: snd_pmac_awacs_put_volume, \
  private_value: (xreg) | (xshift << 8) }

/*
 * mute master/ogain for AWACS: mono
 */
static int snd_pmac_awacs_info_switch(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}
 
static int snd_pmac_awacs_get_switch(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	int mask = kcontrol->private_value & 0xff;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	ucontrol->value.integer.value[0] = (chip->awacs_reg[1] & mask) ? 0 : 1;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_pmac_awacs_put_switch(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	int mask = kcontrol->private_value & 0xff;
	int val, changed;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	val = chip->awacs_reg[1] & ~mask;
	if (! ucontrol->value.integer.value[0])
		val |= mask;
	snd_pmac_awacs_write_reg(chip, 1, val);
	changed = chip->awacs_reg[1] != val;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return changed;
}

#define AWACS_SWITCH(xname, xreg, xmask) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, index: 0, \
  info: snd_pmac_awacs_info_switch, \
  get: snd_pmac_awacs_get_switch, \
  put: snd_pmac_awacs_put_switch, \
  private_value: (xreg) | ((xmask) << 8) }


#ifdef PMAC_AMP_AVAIL
/*
 * Master volume for awacs revision 3
 */
static int snd_pmac_awacs_info_volume_amp(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 31;
	return 0;
}
 
static int snd_pmac_awacs_get_volume_amp(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = chip->amp_vol[0];
	ucontrol->value.integer.value[1] = chip->amp_vol[1];
	return 0;
}

static int snd_pmac_awacs_put_volume_amp(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	int changed = ucontrol->value.integer.value[0] != chip->amp_vol[0] ||
		ucontrol->value.integer.value[1] != chip->amp_vol[1];
	snd_pmac_awacs_enable_amp(chip,
				  ucontrol->value.integer.value[0],
				  ucontrol->value.integer.value[1]);
	return changed;
}
#endif /* PMAC_AMP_AVAIL */


/*
 * lists of mixer elements
 */
static snd_kcontrol_new_t snd_pmac_awacs_mixers1[] = {
	AWACS_VOLUME("Master Playback Volume", 2, 6),
	AWACS_SWITCH("Master Playback Switch", 1, MASK_AMUTE),
	AWACS_SWITCH("Master Capture Switch", 1, MASK_LOOPTHRU),
};

static snd_kcontrol_new_t snd_pmac_awacs_mixers2[] = {
	AWACS_VOLUME("Capture Volume", 0, 4),
	AWACS_SWITCH("Line Capture Switch", 0, MASK_MUX_AUDIN),
	AWACS_SWITCH("CD Capture Switch", 0, MASK_MUX_CD),
	AWACS_SWITCH("Mic Capture Switch", 0, MASK_MUX_MIC),
	AWACS_SWITCH("Mic Boost", 0, MASK_GAINLINE),
};

static snd_kcontrol_new_t snd_pmac_awacs_speaker_mixers[] = {
	AWACS_VOLUME("PC Speaker Playback Volume", 4, 6),
	AWACS_SWITCH("PC Speaker Playback Switch", 1, MASK_CMUTE),
};

#ifdef PMAC_AMP_AVAIL
static snd_kcontrol_new_t snd_pmac_awacs_amp_mixers[] = {
	{
		iface: SNDRV_CTL_ELEM_IFACE_MIXER,
		name: "PC Speaker Playback Volume", index: 0, 
		info: snd_pmac_awacs_info_volume_amp, \
		get: snd_pmac_awacs_get_volume_amp,
		put: snd_pmac_awacs_put_volume_amp,
	},
};
#endif

#define num_controls(ary) (sizeof(ary) / sizeof(snd_kcontrol_new_t))

/*
 * add new mixer elements to the card
 */
static int build_mixers(pmac_t *chip, int nums, snd_kcontrol_new_t *mixers)
{
	int i, err;

	for (i = 0; i < nums; i++) {
		if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&mixers[i], chip))) < 0)
			return err;
	}
	return 0;
}

#ifdef CONFIG_PMAC_PBOOK
static void snd_pmac_awacs_resume(pmac_t *chip)
{
	snd_pmac_awacs_write_reg(chip, 0, chip->awacs_reg[0]);
	snd_pmac_awacs_write_reg(chip, 1, chip->awacs_reg[1]);
	snd_pmac_awacs_write_reg(chip, 2, chip->awacs_reg[2]);
	snd_pmac_awacs_write_reg(chip, 4, chip->awacs_reg[4]);
	if (chip->model == PMAC_SCREAMER) {
		snd_pmac_awacs_write_reg(chip, 5, chip->awacs_reg[5]);
		snd_pmac_awacs_write_reg(chip, 6, chip->awacs_reg[6]);
		snd_pmac_awacs_write_reg(chip, 7, chip->awacs_reg[7]);
		screamer_recalibrate(chip);
	}
}
#endif /* CONFIG_PMAC_PBOOK */

/*
 * initialize chip
 */
int __init
snd_pmac_awacs_init(pmac_t *chip)
{
	int err, vol;

	snd_pmac_awacs_write_reg(chip, 0, MASK_MUX_CD);
	/* FIXME: Only machines with external SRS module need MASK_PAROUT */
	chip->awacs_reg[1] = MASK_LOOPTHRU;
	if (chip->has_iic || chip->device_id == 0x5 ||
	    /*chip->_device_id == 0x8 || */
	    chip->device_id == 0xb)
		chip->awacs_reg[1] |= MASK_PAROUT;
	snd_pmac_awacs_write_reg(chip, 1, chip->awacs_reg[1]);
	/* get default volume from nvram */
	vol = (~nvram_read_byte(0x1308) & 7) << 1;
	snd_pmac_awacs_write_reg(chip, 2, vol + (vol << 6));
	snd_pmac_awacs_write_reg(chip, 4, vol + (vol << 6));
	if (chip->model == PMAC_SCREAMER) {
		snd_pmac_awacs_write_reg(chip, 5, 0);
		snd_pmac_awacs_write_reg(chip, 6, 0);
		snd_pmac_awacs_write_reg(chip, 7, 0);
	}

	/* Recalibrate chip */
	if (chip->model == PMAC_SCREAMER)
		screamer_recalibrate(chip);

	if (chip->model <= PMAC_SCREAMER && chip->revision == 0) {
		chip->revision =
			(in_le32(&chip->awacs->codec_stat) >> 12) & 0xf;
		if (chip->revision == 3) {
#ifdef PMAC_AMP_AVAIL
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
			if (adb_hardware == ADB_VIACUDA)
				chip->amp_only = 1;
#elif defined(CONFIG_ADB_CUDA)
			if (sys_ctrler == SYS_CTRLER_CUDA)
				chip->amp_only = 1;
#endif
			if (chip->amp_only) {
				/*chip->amp_vol[0] = chip->amp_vol[1] = 31;*/
				snd_pmac_awacs_enable_amp(chip, chip->amp_vol[0], chip->amp_vol[1]);
			}
#endif /* PMAC_AMP_AVAIL */
		}
	}

	/*
	 * build mixers
	 */
	strcpy(chip->card->mixername, "PowerMac AWACS");

	if ((err = build_mixers(chip, num_controls(snd_pmac_awacs_mixers1),
				snd_pmac_awacs_mixers1)) < 0)
		return err;
#ifdef PMAC_AMP_AVAIL
	if (chip->amp_only) {
		if ((err = build_mixers(chip, num_controls(snd_pmac_awacs_amp_mixers),
					snd_pmac_awacs_amp_mixers)) < 0)
			return err;
	} else {
#endif /* PMAC_AMP_AVAIL */
		if ((err = build_mixers(chip, num_controls(snd_pmac_awacs_speaker_mixers),
					snd_pmac_awacs_speaker_mixers)) < 0)
			return err;
#ifdef PMAC_AMP_AVAIL
	}
#endif /* PMAC_AMP_AVAIL */
	if ((err = build_mixers(chip, num_controls(snd_pmac_awacs_mixers2),
				snd_pmac_awacs_mixers2)) < 0)
		return err;

	/*
	 * set lowlevel callbacks
	 */
	chip->set_format = snd_pmac_awacs_set_format;
#ifdef CONFIG_PMAC_PBOOK
	chip->resume = snd_pmac_awacs_resume;
#endif

	return 0;
}
