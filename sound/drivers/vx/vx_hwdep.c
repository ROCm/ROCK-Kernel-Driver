/*
 * Driver for Digigram VX soundcards
 *
 * hwdep device manager
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
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

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/hwdep.h>
#include <sound/vx_core.h>

static int vx_hwdep_open(snd_hwdep_t *hw, struct file *file)
{
	return 0;
}

static int vx_hwdep_release(snd_hwdep_t *hw, struct file *file)
{
	return 0;
}

static int vx_hwdep_dsp_status(snd_hwdep_t *hw, snd_hwdep_dsp_status_t *info)
{
	static char *type_ids[VX_TYPE_NUMS] = {
		[VX_TYPE_BOARD] = "vxboard",
		[VX_TYPE_V2] = "vx222",
		[VX_TYPE_MIC] = "vx222",
		[VX_TYPE_VXPOCKET] = "vxpocket",
		[VX_TYPE_VXP440] = "vxp440",
	};
	vx_core_t *vx = snd_magic_cast(vx_core_t, hw->private_data, return -ENXIO);

	snd_assert(type_ids[vx->type], return -EINVAL);
	strcpy(info->id, type_ids[vx->type]);
	if (vx_is_pcmcia(vx))
		info->num_dsps = 4;
	else
		info->num_dsps = 3;
	if (vx->chip_status & VX_STAT_CHIP_INIT)
		info->chip_ready = 1;
	info->version = VX_DRIVER_VERSION;
	return 0;
}

static int vx_hwdep_dsp_load(snd_hwdep_t *hw, snd_hwdep_dsp_image_t *dsp)
{
	vx_core_t *vx = snd_magic_cast(vx_core_t, hw->private_data, return -ENXIO);
	int index, err;

	snd_assert(vx->ops->load_dsp, return -ENXIO);
	err = vx->ops->load_dsp(vx, dsp);
	if (err < 0)
		return err;

	index = dsp->index;
	if (! vx_is_pcmcia(vx))
		index++;
	if (index == 1)
		vx->chip_status |= VX_STAT_XILINX_LOADED;
	if (index < 3)
		return 0;

	/* ok, we reached to the last one */
	/* create the devices if not built yet */
	if (! (vx->chip_status & VX_STAT_DEVICE_INIT)) {
		if ((err = snd_vx_pcm_new(vx)) < 0)
			return err;

		if ((err = snd_vx_mixer_new(vx)) < 0)
			return err;

		if (vx->ops->add_controls)
			if ((err = vx->ops->add_controls(vx)) < 0)
				return err;

		if ((err = snd_card_register(vx->card)) < 0)
			return err;

		vx->chip_status |= VX_STAT_DEVICE_INIT;
	}
	vx->chip_status |= VX_STAT_CHIP_INIT;
	return 0;
}


/* exported */
int snd_vx_hwdep_new(vx_core_t *chip)
{
	int err;
	snd_hwdep_t *hw;

	if ((err = snd_hwdep_new(chip->card, SND_VX_HWDEP_ID, 0, &hw)) < 0)
		return err;

	hw->iface = SNDRV_HWDEP_IFACE_VX;
	hw->private_data = chip;
	hw->ops.open = vx_hwdep_open;
	hw->ops.release = vx_hwdep_release;
	hw->ops.dsp_status = vx_hwdep_dsp_status;
	hw->ops.dsp_load = vx_hwdep_dsp_load;
	hw->exclusive = 1;
	sprintf(hw->name, "VX Loader (%s)", chip->card->driver);
	chip->hwdep = hw;

	return 0;
}
