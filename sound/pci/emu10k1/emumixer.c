/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>,
 *                   Takashi Iwai <tiwai@suse.de>
 *                   Creative Labs, Inc.
 *  Routines for control of EMU10K1 chips / mixer routines
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
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

#define __NO_VERSION__
#include <sound/driver.h>
#include <sound/core.h>
#include <sound/emu10k1.h>

#define chip_t emu10k1_t

static int snd_emu10k1_spdif_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_emu10k1_spdif_get(snd_kcontrol_t * kcontrol,
                                 snd_ctl_elem_value_t * ucontrol)
{
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	int idx = kcontrol->private_value;
	unsigned long flags;

	spin_lock_irqsave(&emu->reg_lock, flags);
	ucontrol->value.iec958.status[0] = (emu->spdif_bits[idx] >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (emu->spdif_bits[idx] >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (emu->spdif_bits[idx] >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (emu->spdif_bits[idx] >> 24) & 0xff;
	spin_unlock_irqrestore(&emu->reg_lock, flags);
        return 0;
}

static int snd_emu10k1_spdif_get_mask(snd_kcontrol_t * kcontrol,
				      snd_ctl_elem_value_t * ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = 0xff;
        return 0;
}

static int snd_emu10k1_spdif_put(snd_kcontrol_t * kcontrol,
                                 snd_ctl_elem_value_t * ucontrol)
{
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	int idx = kcontrol->private_value, change;
	unsigned int val;
	unsigned long flags;

	val = (ucontrol->value.iec958.status[0] << 0) |
	      (ucontrol->value.iec958.status[1] << 8) |
	      (ucontrol->value.iec958.status[2] << 16) |
	      (ucontrol->value.iec958.status[3] << 24);
	spin_lock_irqsave(&emu->reg_lock, flags);
	change = val != emu->spdif_bits[idx];
	if (change) {
		snd_emu10k1_ptr_write(emu, SPCS0 + idx, 0, val);
		emu->spdif_bits[idx] = val;
	}
	spin_unlock_irqrestore(&emu->reg_lock, flags);
        return change;
}

static snd_kcontrol_new_t snd_emu10k1_spdif_mask_control =
{
	access:		SNDRV_CTL_ELEM_ACCESS_READ,
        iface:          SNDRV_CTL_ELEM_IFACE_MIXER,
        name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),
        info:           snd_emu10k1_spdif_info,
        get:            snd_emu10k1_spdif_get_mask
};

static snd_kcontrol_new_t snd_emu10k1_spdif_control =
{
        iface:          SNDRV_CTL_ELEM_IFACE_MIXER,
        name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
        info:           snd_emu10k1_spdif_info,
        get:            snd_emu10k1_spdif_get,
        put:            snd_emu10k1_spdif_put
};

/* FIXME: audigy has more routing/effects */
static int snd_emu10k1_send_routing_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 3*4;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = emu->audigy ? 0x3f : 0x0f;
	return 0;
}

static int snd_emu10k1_send_routing_get(snd_kcontrol_t * kcontrol,
                                        snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	emu10k1_pcm_mixer_t *mix = (emu10k1_pcm_mixer_t *)kcontrol->private_value;
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	int voice, idx;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (voice = 0; voice < 3; voice++)
		for (idx = 0; idx < 4; idx++)
			ucontrol->value.integer.value[(voice * 4) + idx] = emu->audigy ?
				((mix->send_routing[voice] >> (idx * 8)) & 0x3f) :
				((mix->send_routing[voice] >> (idx * 4)) & 0x0f);
	spin_unlock_irqrestore(&emu->reg_lock, flags);
        return 0;
}

static int snd_emu10k1_send_routing_put(snd_kcontrol_t * kcontrol,
                                        snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	emu10k1_pcm_mixer_t *mix = (emu10k1_pcm_mixer_t *)kcontrol->private_value;
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	int change = 0, voice, idx, val;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (voice = 0; voice < 3; voice++)
		for (idx = 0; idx < 4; idx++) {
			val = ucontrol->value.integer.value[(voice * 4) + idx];
			if (emu->audigy) {
				int shift = idx * 8;
				val &= 0x3f;
				if (((mix->send_routing[voice] >> shift) & 0x3f) != val) {
					mix->send_routing[voice] &= ~(0x3f << shift);
					mix->send_routing[voice] |= val << shift;
					change = 1;
				}
			} else {
				int shift = idx * 4;
				val = ucontrol->value.integer.value[(voice * 4) + idx] & 15;
				if (((mix->send_routing[voice] >> shift) & 15) != val) {
					mix->send_routing[voice] &= ~(15 << shift);
					mix->send_routing[voice] |= val << shift;
					change = 1;
				}
			}
		}	
	if (change && mix->epcm) {
		if (mix->epcm->voices[0] && mix->epcm->voices[1]) {
			if (emu->audigy) {
				snd_emu10k1_ptr_write(emu, A_FXRT1, mix->epcm->voices[0]->number, mix->send_routing[1]);
				snd_emu10k1_ptr_write(emu, A_FXRT1, mix->epcm->voices[1]->number, mix->send_routing[2]);
			} else {
				snd_emu10k1_ptr_write(emu, FXRT, mix->epcm->voices[0]->number, mix->send_routing[1] << 16);
				snd_emu10k1_ptr_write(emu, FXRT, mix->epcm->voices[1]->number, mix->send_routing[2] << 16);
			}
		} else if (mix->epcm->voices[0]) {
			if (emu->audigy) {
				snd_emu10k1_ptr_write(emu, A_FXRT1, mix->epcm->voices[0]->number, mix->send_routing[0]);
			} else {
				snd_emu10k1_ptr_write(emu, FXRT, mix->epcm->voices[0]->number, mix->send_routing[0] << 16);
			}
		}
	}
	spin_unlock_irqrestore(&emu->reg_lock, flags);
        return change;
}

static snd_kcontrol_new_t snd_emu10k1_send_routing_control =
{
	access:		SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
        iface:          SNDRV_CTL_ELEM_IFACE_MIXER,
        name:           "EMU10K1 PCM Send Routing",
        info:           snd_emu10k1_send_routing_info,
        get:            snd_emu10k1_send_routing_get,
        put:            snd_emu10k1_send_routing_put
};

static int snd_emu10k1_send_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 3*4;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_emu10k1_send_volume_get(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	emu10k1_pcm_mixer_t *mix = (emu10k1_pcm_mixer_t *)kcontrol->private_value;
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	int idx;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (idx = 0; idx < 3*4; idx++)
		ucontrol->value.integer.value[idx] = mix->send_volume[idx/4][idx%4];
	spin_unlock_irqrestore(&emu->reg_lock, flags);
        return 0;
}

static int snd_emu10k1_send_volume_put(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	emu10k1_pcm_mixer_t *mix = (emu10k1_pcm_mixer_t *)kcontrol->private_value;
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	int change = 0, idx, val;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (idx = 0; idx < 3*4; idx++) {
		val = ucontrol->value.integer.value[idx] & 255;
		if (mix->send_volume[idx/4][idx%4] != val) {
			mix->send_volume[idx/4][idx%4] = val;
			change = 1;
		}
	}
	if (change && mix->epcm) {
		u32 voice;
		if (mix->epcm->voices[0] && mix->epcm->voices[1]) {
			voice = mix->epcm->voices[0]->number;
			snd_emu10k1_ptr_write(emu, PTRX_FXSENDAMOUNT_A, voice, mix->send_volume[1][0]);
			snd_emu10k1_ptr_write(emu, PTRX_FXSENDAMOUNT_B, voice, mix->send_volume[1][1]);
			snd_emu10k1_ptr_write(emu, PSST_FXSENDAMOUNT_C, voice, mix->send_volume[1][2]);
			snd_emu10k1_ptr_write(emu, DSL_FXSENDAMOUNT_D, voice, mix->send_volume[1][3]);
			voice = mix->epcm->voices[1]->number;
			snd_emu10k1_ptr_write(emu, PTRX_FXSENDAMOUNT_A, voice, mix->send_volume[2][0]);
			snd_emu10k1_ptr_write(emu, PTRX_FXSENDAMOUNT_B, voice, mix->send_volume[2][1]);
			snd_emu10k1_ptr_write(emu, PSST_FXSENDAMOUNT_C, voice, mix->send_volume[2][2]);
			snd_emu10k1_ptr_write(emu, DSL_FXSENDAMOUNT_D, voice, mix->send_volume[2][3]);
		} else if (mix->epcm->voices[0]) {
			voice = mix->epcm->voices[0]->number;
			snd_emu10k1_ptr_write(emu, PTRX_FXSENDAMOUNT_A, voice, mix->send_volume[0][0]);
			snd_emu10k1_ptr_write(emu, PTRX_FXSENDAMOUNT_B, voice, mix->send_volume[0][1]);
			snd_emu10k1_ptr_write(emu, PSST_FXSENDAMOUNT_C, voice, mix->send_volume[0][2]);
			snd_emu10k1_ptr_write(emu, DSL_FXSENDAMOUNT_D, voice, mix->send_volume[0][3]);
		}
	}
	spin_unlock_irqrestore(&emu->reg_lock, flags);
        return change;
}

static snd_kcontrol_new_t snd_emu10k1_send_volume_control =
{
	access:		SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
        iface:          SNDRV_CTL_ELEM_IFACE_MIXER,
        name:           "EMU10K1 PCM Send Volume",
        info:           snd_emu10k1_send_volume_info,
        get:            snd_emu10k1_send_volume_get,
        put:            snd_emu10k1_send_volume_put
};

static int snd_emu10k1_attn_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 3;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xffff;
	return 0;
}

static int snd_emu10k1_attn_get(snd_kcontrol_t * kcontrol,
                                snd_ctl_elem_value_t * ucontrol)
{
	emu10k1_pcm_mixer_t *mix = (emu10k1_pcm_mixer_t *)kcontrol->private_value;
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int idx;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (idx = 0; idx < 3; idx++)
		ucontrol->value.integer.value[idx] = mix->attn[idx];
	spin_unlock_irqrestore(&emu->reg_lock, flags);
        return 0;
}

static int snd_emu10k1_attn_put(snd_kcontrol_t * kcontrol,
				snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	emu10k1_pcm_mixer_t *mix = (emu10k1_pcm_mixer_t *)kcontrol->private_value;
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	int change = 0, idx, val;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (idx = 0; idx < 3; idx++) {
		val = ucontrol->value.integer.value[idx] & 0xffff;
		if (mix->attn[idx] != val) {
			mix->attn[idx] = val;
			change = 1;
		}
	}
	if (change && mix->epcm) {
		if (mix->epcm->voices[0] && mix->epcm->voices[1]) {
			snd_emu10k1_ptr_write(emu, VTFT_VOLUMETARGET, mix->epcm->voices[0]->number, mix->attn[1]);
			snd_emu10k1_ptr_write(emu, VTFT_VOLUMETARGET, mix->epcm->voices[1]->number, mix->attn[2]);
		} else if (mix->epcm->voices[0]) {
			snd_emu10k1_ptr_write(emu, VTFT_VOLUMETARGET, mix->epcm->voices[0]->number, mix->attn[0]);
		}
	}
	spin_unlock_irqrestore(&emu->reg_lock, flags);
        return change;
}

static snd_kcontrol_new_t snd_emu10k1_attn_control =
{
	access:		SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
        iface:          SNDRV_CTL_ELEM_IFACE_MIXER,
        name:           "EMU10K1 PCM Volume",
        info:           snd_emu10k1_attn_info,
        get:            snd_emu10k1_attn_get,
        put:            snd_emu10k1_attn_put
};

static int snd_emu10k1_shared_spdif_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_emu10k1_shared_spdif_get(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = inl(emu->port + HCFG) & HCFG_GPOUT0 ? 0 : 1;
        return 0;
}

static int snd_emu10k1_shared_spdif_put(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	unsigned int reg, val;
	int change;

	spin_lock_irqsave(&emu->reg_lock, flags);
	reg = inl(emu->port + HCFG);
	val = ucontrol->value.integer.value[0] & 1 ? 0 : HCFG_GPOUT0;
	change = (reg & HCFG_GPOUT0) != val;
	reg &= ~HCFG_GPOUT0;
	reg |= val;
	outl(reg | val, emu->port + HCFG);
	spin_unlock_irqrestore(&emu->reg_lock, flags);
        return change;
}

static snd_kcontrol_new_t snd_emu10k1_shared_spdif =
{
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:		"SB Live Analog/Digital Output Jack",
	info:		snd_emu10k1_shared_spdif_info,
	get:		snd_emu10k1_shared_spdif_get,
	put:		snd_emu10k1_shared_spdif_put
};

static void snd_emu10k1_mixer_free_ac97(ac97_t *ac97)
{
	emu10k1_t *emu = snd_magic_cast(emu10k1_t, ac97->private_data, return);
	emu->ac97 = NULL;
}

int __devinit snd_emu10k1_mixer(emu10k1_t *emu)
{
	ac97_t ac97;
	int err, pcm, idx;
	snd_kcontrol_t *kctl;
	snd_card_t *card = emu->card;

	if (!emu->APS) {
		memset(&ac97, 0, sizeof(ac97));
		ac97.write = snd_emu10k1_ac97_write;
		ac97.read = snd_emu10k1_ac97_read;
		ac97.private_data = emu;
		ac97.private_free = snd_emu10k1_mixer_free_ac97;
		if ((err = snd_ac97_mixer(emu->card, &ac97, &emu->ac97)) < 0)
			return err;
	} else {
		strcpy(emu->card->mixername, "EMU APS");
	}

	for (pcm = 0; pcm < 32; pcm++) {
		emu10k1_pcm_mixer_t *mix;
		
		mix = &emu->pcm_mixer[pcm];
		mix->epcm = NULL;

		if ((kctl = mix->ctl_send_routing = snd_ctl_new1(&snd_emu10k1_send_routing_control, emu)) == NULL)
			return -ENOMEM;
		kctl->private_value = (long)mix;
		kctl->id.index = pcm;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
		if (emu->audigy) {
			mix->send_routing[0] = mix->send_routing[1] = mix->send_routing[2] = 0x3210;
		} else {
			mix->send_routing[0] = mix->send_routing[1] = mix->send_routing[2] = 0x03020100;
		}
		
		if ((kctl = mix->ctl_send_volume = snd_ctl_new1(&snd_emu10k1_send_volume_control, emu)) == NULL)
			return -ENOMEM;
		kctl->private_value = (long)mix;
		kctl->id.index = pcm;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
		memset(&mix->send_volume, 0, sizeof(mix->send_volume));
		mix->send_volume[0][0] = mix->send_volume[0][1] =
		mix->send_volume[1][0] = mix->send_volume[2][1] = 255;
		
		if ((kctl = mix->ctl_attn = snd_ctl_new1(&snd_emu10k1_attn_control, emu)) == NULL)
			return -ENOMEM;
		kctl->private_value = (long)mix;
		kctl->id.index = pcm;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
		mix->attn[0] = mix->attn[1] = mix->attn[2] = 0xffff;
	}
	
	for (idx = 0; idx < 3; idx++) {
		if ((kctl = snd_ctl_new1(&snd_emu10k1_spdif_mask_control, emu)) == NULL)
			return -ENOMEM;
		kctl->private_value = idx;
		kctl->id.index = idx;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
		if ((kctl = snd_ctl_new1(&snd_emu10k1_spdif_control, emu)) == NULL)
			return -ENOMEM;
		kctl->private_value = idx;
		kctl->id.index = idx;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
	}

	if ((kctl = snd_ctl_new1(&snd_emu10k1_shared_spdif, emu)) == NULL)
		return -ENOMEM;
	if ((err = snd_ctl_add(card, kctl)))
		return err;

	return 0;
}
