/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.2
 *  by Intel Corporation (http://developer.intel.com) and to datasheets
 *  for specific codecs.
 *
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
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/ac97_codec.h>
#include <sound/asoundef.h>
#include "ac97_patch.h"
#include "ac97_id.h"
#include "ac97_local.h"

#define chip_t ac97_t

/*
 *  PCM support
 */

static unsigned char rate_reg_tables[2][4][9] = {
{
  /* standard rates */
  {
  	/* 3&4 front, 7&8 rear, 6&9 center/lfe */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 3 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 4 */
	0xff,				/* slot 5 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 6 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 7 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 8 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 9 */
	0xff,				/* slot 10 */
	0xff,				/* slot 11 */
  },
  {
  	/* 7&8 front, 6&9 rear, 10&11 center/lfe */
	0xff,				/* slot 3 */
	0xff,				/* slot 4 */
	0xff,				/* slot 5 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 6 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 7 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 8 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 9 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 10 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 11 */
  },
  {
  	/* 6&9 front, 10&11 rear, 3&4 center/lfe */
	AC97_PCM_LFE_DAC_RATE,		/* slot 3 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 4 */
	0xff,				/* slot 5 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 6 */
	0xff,				/* slot 7 */
	0xff,				/* slot 8 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 9 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 10 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 11 */
  },
  {
  	/* 10&11 front, 3&4 rear, 7&8 center/lfe */
	AC97_PCM_SURR_DAC_RATE,		/* slot 3 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 4 */
	0xff,				/* slot 5 */
	0xff,				/* slot 6 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 7 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 8 */
	0xff,				/* slot 9 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 10 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 11 */
  },
},
{
  /* FIXME: double rates */
  {
  	/* 3&4 front, 7&8 rear, 6&9 center/lfe */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 3 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 4 */
	0xff,				/* slot 5 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 6 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 7 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 8 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 9 */
	0xff,				/* slot 10 */
	0xff,				/* slot 11 */
  },
  {
  	/* 7&8 front, 6&9 rear, 10&11 center/lfe */
	0xff,				/* slot 3 */
	0xff,				/* slot 4 */
	0xff,				/* slot 5 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 6 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 7 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 8 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 9 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 10 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 11 */
  },
  {
  	/* 6&9 front, 10&11 rear, 3&4 center/lfe */
	AC97_PCM_LFE_DAC_RATE,		/* slot 3 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 4 */
	0xff,				/* slot 5 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 6 */
	0xff,				/* slot 7 */
	0xff,				/* slot 8 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 9 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 10 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 11 */
  },
  {
  	/* 10&11 front, 3&4 rear, 7&8 center/lfe */
	AC97_PCM_SURR_DAC_RATE,		/* slot 3 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 4 */
	0xff,				/* slot 5 */
	0xff,				/* slot 6 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 7 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 8 */
	0xff,				/* slot 9 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 10 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 11 */
  }
}};

/* FIXME: more various mappings for ADC? */
static unsigned char rate_cregs[9] = {
	AC97_PCM_LR_ADC_RATE,	/* 3 */
	AC97_PCM_LR_ADC_RATE,	/* 4 */
	0xff,			/* 5 */
	AC97_PCM_MIC_ADC_RATE,	/* 6 */
	0xff,			/* 7 */
	0xff,			/* 8 */
	0xff,			/* 9 */
	0xff,			/* 10 */
	0xff,			/* 11 */
};

static unsigned char get_slot_reg(struct ac97_pcm *pcm, unsigned short cidx,
				  unsigned short slot, int dbl)
{
	if (slot < 3)
		return 0xff;
	if (slot > 11)
		return 0xff;
	if (pcm->spdif)
		return AC97_SPDIF; /* pseudo register */
	if (pcm->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return rate_reg_tables[dbl][pcm->r[dbl].rate_table[cidx]][slot - 3];
	else
		return rate_cregs[slot - 3];
}

static int set_spdif_rate(ac97_t *ac97, unsigned short rate)
{
	unsigned short old, bits, reg, mask;
	unsigned int sbits;

	if (! (ac97->ext_id & AC97_EI_SPDIF))
		return -ENODEV;

	if (ac97->flags & AC97_CS_SPDIF) {
		switch (rate) {
		case 48000: bits = 0; break;
		case 44100: bits = 1 << AC97_SC_SPSR_SHIFT; break;
		default: /* invalid - disable output */
			snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0);
			return -EINVAL;
		}
		reg = AC97_CSR_SPDIF;
		mask = 1 << AC97_SC_SPSR_SHIFT;
	} else {
		if (ac97->id == AC97_ID_CM9739 && rate != 48000) {
			snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0);
			return -EINVAL;
		}
		switch (rate) {
		case 44100: bits = AC97_SC_SPSR_44K; break;
		case 48000: bits = AC97_SC_SPSR_48K; break;
		case 32000: bits = AC97_SC_SPSR_32K; break;
		default: /* invalid - disable output */
			snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0);
			return -EINVAL;
		}
		reg = AC97_SPDIF;
		mask = AC97_SC_SPSR_MASK;
	}

	spin_lock(&ac97->reg_lock);
	old = snd_ac97_read(ac97, reg) & mask;
	spin_unlock(&ac97->reg_lock);
	if (old != bits) {
		snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0);
		snd_ac97_update_bits(ac97, reg, mask, bits);
		/* update the internal spdif bits */
		spin_lock(&ac97->reg_lock);
		sbits = ac97->spdif_status;
		if (sbits & IEC958_AES0_PROFESSIONAL) {
			sbits &= ~IEC958_AES0_PRO_FS;
			switch (rate) {
			case 44100: sbits |= IEC958_AES0_PRO_FS_44100; break;
			case 48000: sbits |= IEC958_AES0_PRO_FS_48000; break;
			case 32000: sbits |= IEC958_AES0_PRO_FS_32000; break;
			}
		} else {
			sbits &= ~(IEC958_AES3_CON_FS << 24);
			switch (rate) {
			case 44100: sbits |= IEC958_AES3_CON_FS_44100<<24; break;
			case 48000: sbits |= IEC958_AES3_CON_FS_48000<<24; break;
			case 32000: sbits |= IEC958_AES3_CON_FS_32000<<24; break;
			}
		}
		ac97->spdif_status = sbits;
		spin_unlock(&ac97->reg_lock);
	}
	snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, AC97_EA_SPDIF);
	return 0;
}

/**
 * snd_ac97_set_rate - change the rate of the given input/output.
 * @ac97: the ac97 instance
 * @reg: the register to change
 * @rate: the sample rate to set
 *
 * Changes the rate of the given input/output on the codec.
 * If the codec doesn't support VAR, the rate must be 48000 (except
 * for SPDIF).
 *
 * The valid registers are AC97_PMC_MIC_ADC_RATE,
 * AC97_PCM_FRONT_DAC_RATE, AC97_PCM_LR_ADC_RATE.
 * AC97_PCM_SURR_DAC_RATE and AC97_PCM_LFE_DAC_RATE are accepted
 * if the codec supports them.
 * AC97_SPDIF is accepted as a pseudo register to modify the SPDIF
 * status bits.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_ac97_set_rate(ac97_t *ac97, int reg, unsigned short rate)
{
	unsigned int tmp;
	
	switch (reg) {
	case AC97_PCM_MIC_ADC_RATE:
		if ((ac97->regs[AC97_EXTENDED_STATUS] & AC97_EA_VRM) == 0)	/* MIC VRA */
			if (rate != 48000)
				return -EINVAL;
		break;
	case AC97_PCM_FRONT_DAC_RATE:
	case AC97_PCM_LR_ADC_RATE:
		if ((ac97->regs[AC97_EXTENDED_STATUS] & AC97_EA_VRA) == 0)	/* VRA */
			if (rate != 48000)
				return -EINVAL;
		break;
	case AC97_PCM_SURR_DAC_RATE:
		if (! (ac97->scaps & AC97_SCAP_SURROUND_DAC))
			return -EINVAL;
		break;
	case AC97_PCM_LFE_DAC_RATE:
		if (! (ac97->scaps & AC97_SCAP_CENTER_LFE_DAC))
			return -EINVAL;
		break;
	case AC97_SPDIF:
		/* special case */
		return set_spdif_rate(ac97, rate);
	default:
		return -EINVAL;
	}
	tmp = ((unsigned int)rate * ac97->bus->clock) / 48000;
	if (tmp > 65535)
		return -EINVAL;
	snd_ac97_update(ac97, reg, tmp & 0xffff);
	snd_ac97_read(ac97, reg);
	return 0;
}

static unsigned short get_pslots(ac97_t *ac97, unsigned char *rate_table, unsigned short *spdif_slots)
{
	if (!ac97_is_audio(ac97))
		return 0;
	if (ac97_is_rev22(ac97) || ac97_can_amap(ac97)) {
		unsigned short slots = 0;
		if (ac97_is_rev22(ac97)) {
			/* Note: it's simply emulation of AMAP behaviour */
			u16 es;
			es = ac97->regs[AC97_EXTENDED_STATUS] &= ~AC97_EI_DACS_SLOT_MASK;
			switch (ac97->addr) {
			case 1:
			case 2: es |= (1<<AC97_EI_DACS_SLOT_SHIFT); break;
			case 3: es |= (2<<AC97_EI_DACS_SLOT_SHIFT); break;
			}
			snd_ac97_write_cache(ac97, AC97_EXTENDED_STATUS, es);
		}
		switch (ac97->addr) {
		case 0:
			slots |= (1<<AC97_SLOT_PCM_LEFT)|(1<<AC97_SLOT_PCM_RIGHT);
			if (ac97->scaps & AC97_SCAP_SURROUND_DAC)
				slots |= (1<<AC97_SLOT_PCM_SLEFT)|(1<<AC97_SLOT_PCM_SRIGHT);
			if (ac97->scaps & AC97_SCAP_CENTER_LFE_DAC)
				slots |= (1<<AC97_SLOT_PCM_CENTER)|(1<<AC97_SLOT_LFE);
			if (ac97->ext_id & AC97_EI_SPDIF) {
				if (!(ac97->scaps & AC97_SCAP_SURROUND_DAC))
					*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT)|(1<<AC97_SLOT_SPDIF_RIGHT);
				else if (!(ac97->scaps & AC97_SCAP_CENTER_LFE_DAC))
					*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT1)|(1<<AC97_SLOT_SPDIF_RIGHT1);
				else
					*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT2)|(1<<AC97_SLOT_SPDIF_RIGHT2);
			}
			*rate_table = 0;
			break;
		case 1:
		case 2:
			slots |= (1<<AC97_SLOT_PCM_SLEFT)|(1<<AC97_SLOT_PCM_SRIGHT);
			if (ac97->scaps & AC97_SCAP_SURROUND_DAC)
				slots |= (1<<AC97_SLOT_PCM_CENTER)|(1<<AC97_SLOT_LFE);
			if (ac97->ext_id & AC97_EI_SPDIF) {
				if (!(ac97->scaps & AC97_SCAP_SURROUND_DAC))
					*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT1)|(1<<AC97_SLOT_SPDIF_RIGHT1);
				else
					*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT2)|(1<<AC97_SLOT_SPDIF_RIGHT2);
			}
			*rate_table = 1;
			break;
		case 3:
			slots |= (1<<AC97_SLOT_PCM_CENTER)|(1<<AC97_SLOT_LFE);
			if (ac97->ext_id & AC97_EI_SPDIF)
				*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT2)|(1<<AC97_SLOT_SPDIF_RIGHT2);
			*rate_table = 2;
			break;
		}
		return slots;
	} else {
		unsigned short slots;
		slots = (1<<AC97_SLOT_PCM_LEFT)|(1<<AC97_SLOT_PCM_RIGHT);
		if (ac97->scaps & AC97_SCAP_SURROUND_DAC)
			slots |= (1<<AC97_SLOT_PCM_SLEFT)|(1<<AC97_SLOT_PCM_SRIGHT);
		if (ac97->scaps & AC97_SCAP_CENTER_LFE_DAC)
			slots |= (1<<AC97_SLOT_PCM_CENTER)|(1<<AC97_SLOT_LFE);
		if (ac97->ext_id & AC97_EI_SPDIF) {
			if (!(ac97->scaps & AC97_SCAP_SURROUND_DAC))
				*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT)|(1<<AC97_SLOT_SPDIF_RIGHT);
			else if (!(ac97->scaps & AC97_SCAP_CENTER_LFE_DAC))
				*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT1)|(1<<AC97_SLOT_SPDIF_RIGHT1);
			else
				*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT2)|(1<<AC97_SLOT_SPDIF_RIGHT2);
		}
		*rate_table = 0;
		return slots;
	}
}

static unsigned short get_cslots(ac97_t *ac97)
{
	unsigned short slots;

	if (!ac97_is_audio(ac97))
		return 0;
	slots = (1<<AC97_SLOT_PCM_LEFT)|(1<<AC97_SLOT_PCM_RIGHT);
	slots |= (1<<AC97_SLOT_MIC);
	return slots;
}

static unsigned int get_rates(struct ac97_pcm *pcm, unsigned int cidx, unsigned short slots, int dbl)
{
	int i, idx;
	unsigned int rates = ~0;
	unsigned char reg;

	for (i = 3; i < 12; i++) {
		if (!(slots & (1 << i)))
			continue;
		reg = get_slot_reg(pcm, cidx, i, dbl);
		switch (reg) {
		case AC97_PCM_FRONT_DAC_RATE:	idx = AC97_RATES_FRONT_DAC; break;
		case AC97_PCM_SURR_DAC_RATE:	idx = AC97_RATES_SURR_DAC; break;
		case AC97_PCM_LFE_DAC_RATE:	idx = AC97_RATES_LFE_DAC; break;
		case AC97_PCM_LR_ADC_RATE:	idx = AC97_RATES_ADC; break;
		case AC97_PCM_MIC_ADC_RATE:	idx = AC97_RATES_MIC_ADC; break;
		default:			idx = AC97_RATES_SPDIF; break;
		}
		rates &= pcm->r[dbl].codec[cidx]->rates[idx];
	}
	return rates;
}

/**
 * snd_ac97_pcm_assign - assign AC97 slots to given PCM streams
 * @bus: the ac97 bus instance
 * @pcms_count: count of PCMs to be assigned
 * @pcms: PCMs to be assigned
 *
 * It assigns available AC97 slots for given PCMs. If none or only
 * some slots are available, pcm->xxx.slots and pcm->xxx.rslots[] members
 * are reduced and might be zero.
 */
int snd_ac97_pcm_assign(ac97_bus_t *bus,
			unsigned short pcms_count,
			const struct ac97_pcm *pcms)
{
	int i, j, k;
	const struct ac97_pcm *pcm;
	struct ac97_pcm *rpcms, *rpcm;
	unsigned short avail_slots[2][4];
	unsigned char rate_table[2][4];
	unsigned short tmp, slots;
	unsigned short spdif_slots[4];
	unsigned int rates;
	ac97_t *codec;

	rpcms = snd_kcalloc(sizeof(struct ac97_pcm) * pcms_count, GFP_KERNEL);
	if (rpcms == NULL)
		return -ENOMEM;
	memset(avail_slots, 0, sizeof(avail_slots));
	memset(rate_table, 0, sizeof(rate_table));
	memset(spdif_slots, 0, sizeof(spdif_slots));
	for (i = 0; i < 4; i++) {
		codec = bus->codec[i];
		if (!codec)
			continue;
		avail_slots[0][i] = get_pslots(codec, &rate_table[0][i], &spdif_slots[i]);
		avail_slots[1][i] = get_cslots(codec);
		if (!(codec->scaps & AC97_SCAP_INDEP_SDIN)) {
			for (j = 0; j < i; j++) {
				if (bus->codec[j])
					avail_slots[1][i] &= ~avail_slots[1][j];
			}
		}
	}
	/* FIXME: add double rate allocation */
	/* first step - exclusive devices */
	for (i = 0; i < pcms_count; i++) {
		pcm = &pcms[i];
		rpcm = &rpcms[i];
		/* low-level driver thinks that it's more clever */
		if (pcm->copy_flag) {
			*rpcm = *pcm;
			continue;
		}
		rpcm->stream = pcm->stream;
		rpcm->exclusive = pcm->exclusive;
		rpcm->spdif = pcm->spdif;
		rpcm->private_value = pcm->private_value;
		rpcm->bus = bus;
		rpcm->rates = ~0;
		slots = pcm->r[0].slots;
		for (j = 0; j < 4 && slots; j++) {
			if (!bus->codec[j])
				continue;
			rates = ~0;
			if (pcm->spdif && pcm->stream == 0)
				tmp = spdif_slots[j];
			else
				tmp = avail_slots[pcm->stream][j];
			if (pcm->exclusive) {
				/* exclusive access */
				tmp &= slots;
				for (k = 0; k < i; k++) {
					if (rpcm->stream == rpcms[k].stream)
						tmp &= ~rpcms[k].r[0].rslots[j];
				}
			} else {
				/* non-exclusive access */
				tmp &= pcm->r[0].slots;
			}
			if (tmp) {
				rpcm->r[0].rslots[j] = tmp;
				rpcm->r[0].codec[j] = bus->codec[j];
				rpcm->r[0].rate_table[j] = rate_table[pcm->stream][j];
				rates = get_rates(rpcm, j, tmp, 0);
				if (pcm->exclusive)
					avail_slots[pcm->stream][j] &= ~tmp;
			}
			slots &= ~tmp;
			rpcm->r[0].slots |= tmp;
			rpcm->rates &= rates;
		}
		if (rpcm->rates == ~0)
			rpcm->rates = 0; /* not used */
	}
	bus->pcms_count = pcms_count;
	bus->pcms = rpcms;
	return 0;
}

/**
 * snd_ac97_pcm_open - opens the given AC97 pcm
 * @pcm: the ac97 pcm instance
 * @rate: rate in Hz, if codec does not support VRA, this value must be 48000Hz
 * @cfg: output stream characteristics
 * @slots: a subset of allocated slots (snd_ac97_pcm_assign) for this pcm
 *
 * It locks the specified slots and sets the given rate to AC97 registers.
 */
int snd_ac97_pcm_open(struct ac97_pcm *pcm, unsigned int rate,
		      enum ac97_pcm_cfg cfg, unsigned short slots)
{
	ac97_bus_t *bus;
	int i, cidx, r = 0, ok_flag;
	unsigned int reg_ok = 0;
	unsigned char reg;
	int err = 0;

	if (rate > 48000)	/* FIXME: add support for double rate */
		return -EINVAL;
	bus = pcm->bus;
	if (cfg == AC97_PCM_CFG_SPDIF) {
		int err;
		for (cidx = 0; cidx < 4; cidx++)
			if (bus->codec[cidx] && (bus->codec[cidx]->ext_id & AC97_EI_SPDIF)) {
				err = set_spdif_rate(bus->codec[cidx], rate);
				if (err < 0)
					return err;
			}
	}
	spin_lock_irq(&pcm->bus->bus_lock);
	for (i = 3; i < 12; i++) {
		if (!(slots & (1 << i)))
			continue;
		ok_flag = 0;
		for (cidx = 0; cidx < 4; cidx++) {
			if (bus->used_slots[pcm->stream][cidx] & (1 << i)) {
				spin_unlock_irq(&pcm->bus->bus_lock);
				err = -EBUSY;
				goto error;
			}
			if (pcm->r[r].rslots[cidx] & (1 << i)) {
				bus->used_slots[pcm->stream][cidx] |= (1 << i);
				ok_flag++;
			}
		}
		if (!ok_flag) {
			spin_unlock_irq(&pcm->bus->bus_lock);
			snd_printk(KERN_ERR "cannot find configuration for AC97 slot %i\n", i);
			err = -EAGAIN;
			goto error;
		}
	}
	spin_unlock_irq(&pcm->bus->bus_lock);
	for (i = 3; i < 12; i++) {
		if (!(slots & (1 << i)))
			continue;
		for (cidx = 0; cidx < 4; cidx++) {
			if (pcm->r[r].rslots[cidx] & (1 << i)) {
				reg = get_slot_reg(pcm, cidx, i, r);
				if (reg == 0xff) {
					snd_printk(KERN_ERR "invalid AC97 slot %i?\n", i);
					continue;
				}
				if (reg_ok & (1 << (reg - AC97_PCM_FRONT_DAC_RATE)))
					continue;
				//printk(KERN_DEBUG "setting ac97 reg 0x%x to rate %d\n", reg, rate);
				err = snd_ac97_set_rate(pcm->r[r].codec[cidx], reg, rate);
				if (err < 0)
					snd_printk(KERN_ERR "error in snd_ac97_set_rate: cidx=%d, reg=0x%x, rate=%d, err=%d\n", cidx, reg, rate, err);
				else
					reg_ok |= (1 << (reg - AC97_PCM_FRONT_DAC_RATE));
			}
		}
	}
	pcm->aslots = slots;
	return 0;

 error:
	pcm->aslots = slots;
	snd_ac97_pcm_close(pcm);
	return err;
}

/**
 * snd_ac97_pcm_close - closes the given AC97 pcm
 * @pcm: the ac97 pcm instance
 *
 * It frees the locked AC97 slots.
 */
int snd_ac97_pcm_close(struct ac97_pcm *pcm)
{
	ac97_bus_t *bus;
	unsigned short slots = pcm->aslots;
	int i, cidx;

	bus = pcm->bus;
	spin_lock_irq(&pcm->bus->bus_lock);
	for (i = 3; i < 12; i++) {
		if (!(slots & (1 << i)))
			continue;
		for (cidx = 0; cidx < 4; cidx++)
			bus->used_slots[pcm->stream][cidx] &= ~(1 << i);
	}
	pcm->aslots = 0;
	spin_unlock_irq(&pcm->bus->bus_lock);
	return 0;
}
