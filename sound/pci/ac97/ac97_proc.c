/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.2
 *  by Intel Corporation (http://developer.intel.com).
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
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/ac97_codec.h>
#include <sound/asoundef.h>
#include "ac97_local.h"
#include "ac97_id.h"

/*
 * proc interface
 */

static void snd_ac97_proc_read_main(ac97_t *ac97, snd_info_buffer_t * buffer, int subidx)
{
	char name[64];
	unsigned int id;
	unsigned short val, tmp, ext, mext;
	static const char *spdif_slots[4] = { " SPDIF=3/4", " SPDIF=7/8", " SPDIF=6/9", " SPDIF=res" };
	static const char *spdif_rates[4] = { " Rate=44.1kHz", " Rate=res", " Rate=48kHz", " Rate=32kHz" };
	static const char *spdif_rates_cs4205[4] = { " Rate=48kHz", " Rate=44.1kHz", " Rate=res", " Rate=res" };

	id = snd_ac97_read(ac97, AC97_VENDOR_ID1) << 16;
	id |= snd_ac97_read(ac97, AC97_VENDOR_ID2);
	snd_ac97_get_name(NULL, id, name, 0);
	snd_iprintf(buffer, "%d-%d/%d: %s\n\n", ac97->addr, ac97->num, subidx, name);
	if ((ac97->scaps & AC97_SCAP_AUDIO) == 0)
		goto __modem;

	// val = snd_ac97_read(ac97, AC97_RESET);
	val = ac97->caps;
	snd_iprintf(buffer, "Capabilities     :%s%s%s%s%s%s\n",
	    	    val & AC97_BC_DEDICATED_MIC ? " -dedicated MIC PCM IN channel-" : "",
		    val & AC97_BC_RESERVED1 ? " -reserved1-" : "",
		    val & AC97_BC_BASS_TREBLE ? " -bass & treble-" : "",
		    val & AC97_BC_SIM_STEREO ? " -simulated stereo-" : "",
		    val & AC97_BC_HEADPHONE ? " -headphone out-" : "",
		    val & AC97_BC_LOUDNESS ? " -loudness-" : "");
	tmp = ac97->caps & AC97_BC_DAC_MASK;
	snd_iprintf(buffer, "DAC resolution   : %s%s%s%s\n",
		    tmp == AC97_BC_16BIT_DAC ? "16-bit" : "",
		    tmp == AC97_BC_18BIT_DAC ? "18-bit" : "",
		    tmp == AC97_BC_20BIT_DAC ? "20-bit" : "",
		    tmp == AC97_BC_DAC_MASK ? "???" : "");
	tmp = ac97->caps & AC97_BC_ADC_MASK;
	snd_iprintf(buffer, "ADC resolution   : %s%s%s%s\n",
		    tmp == AC97_BC_16BIT_ADC ? "16-bit" : "",
		    tmp == AC97_BC_18BIT_ADC ? "18-bit" : "",
		    tmp == AC97_BC_20BIT_ADC ? "20-bit" : "",
		    tmp == AC97_BC_ADC_MASK ? "???" : "");
	snd_iprintf(buffer, "3D enhancement   : %s\n",
		snd_ac97_stereo_enhancements[(val >> 10) & 0x1f]);
	snd_iprintf(buffer, "\nCurrent setup\n");
	val = snd_ac97_read(ac97, AC97_MIC);
	snd_iprintf(buffer, "Mic gain         : %s [%s]\n", val & 0x0040 ? "+20dB" : "+0dB", ac97->regs[AC97_MIC] & 0x0040 ? "+20dB" : "+0dB");
	val = snd_ac97_read(ac97, AC97_GENERAL_PURPOSE);
	snd_iprintf(buffer, "POP path         : %s 3D\n"
		    "Sim. stereo      : %s\n"
		    "3D enhancement   : %s\n"
		    "Loudness         : %s\n"
		    "Mono output      : %s\n"
		    "Mic select       : %s\n"
		    "ADC/DAC loopback : %s\n",
		    val & 0x8000 ? "post" : "pre",
		    val & 0x4000 ? "on" : "off",
		    val & 0x2000 ? "on" : "off",
		    val & 0x1000 ? "on" : "off",
		    val & 0x0200 ? "Mic" : "MIX",
		    val & 0x0100 ? "Mic2" : "Mic1",
		    val & 0x0080 ? "on" : "off");

	ext = snd_ac97_read(ac97, AC97_EXTENDED_ID);
	if (ext == 0)
		goto __modem;
		
	snd_iprintf(buffer, "Extended ID      : codec=%i rev=%i%s%s%s%s DSA=%i%s%s%s%s\n",
			(ext & AC97_EI_ADDR_MASK) >> AC97_EI_ADDR_SHIFT,
			(ext & AC97_EI_REV_MASK) >> AC97_EI_REV_SHIFT,
			ext & AC97_EI_AMAP ? " AMAP" : "",
			ext & AC97_EI_LDAC ? " LDAC" : "",
			ext & AC97_EI_SDAC ? " SDAC" : "",
			ext & AC97_EI_CDAC ? " CDAC" : "",
			(ext & AC97_EI_DACS_SLOT_MASK) >> AC97_EI_DACS_SLOT_SHIFT,
			ext & AC97_EI_VRM ? " VRM" : "",
			ext & AC97_EI_SPDIF ? " SPDIF" : "",
			ext & AC97_EI_DRA ? " DRA" : "",
			ext & AC97_EI_VRA ? " VRA" : "");
	val = snd_ac97_read(ac97, AC97_EXTENDED_STATUS);
	snd_iprintf(buffer, "Extended status  :%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
			val & AC97_EA_PRL ? " PRL" : "",
			val & AC97_EA_PRK ? " PRK" : "",
			val & AC97_EA_PRJ ? " PRJ" : "",
			val & AC97_EA_PRI ? " PRI" : "",
			val & AC97_EA_SPCV ? " SPCV" : "",
			val & AC97_EA_MDAC ? " MADC" : "",
			val & AC97_EA_LDAC ? " LDAC" : "",
			val & AC97_EA_SDAC ? " SDAC" : "",
			val & AC97_EA_CDAC ? " CDAC" : "",
			ext & AC97_EI_SPDIF ? spdif_slots[(val & AC97_EA_SPSA_SLOT_MASK) >> AC97_EA_SPSA_SLOT_SHIFT] : "",
			val & AC97_EA_VRM ? " VRM" : "",
			val & AC97_EA_SPDIF ? " SPDIF" : "",
			val & AC97_EA_DRA ? " DRA" : "",
			val & AC97_EA_VRA ? " VRA" : "");
	if (ext & AC97_EI_VRA) {	/* VRA */
		val = snd_ac97_read(ac97, AC97_PCM_FRONT_DAC_RATE);
		snd_iprintf(buffer, "PCM front DAC    : %iHz\n", val);
		if (ext & AC97_EI_SDAC) {
			val = snd_ac97_read(ac97, AC97_PCM_SURR_DAC_RATE);
			snd_iprintf(buffer, "PCM Surr DAC     : %iHz\n", val);
		}
		if (ext & AC97_EI_LDAC) {
			val = snd_ac97_read(ac97, AC97_PCM_LFE_DAC_RATE);
			snd_iprintf(buffer, "PCM LFE DAC      : %iHz\n", val);
		}
		val = snd_ac97_read(ac97, AC97_PCM_LR_ADC_RATE);
		snd_iprintf(buffer, "PCM ADC          : %iHz\n", val);
	}
	if (ext & AC97_EI_VRM) {
		val = snd_ac97_read(ac97, AC97_PCM_MIC_ADC_RATE);
		snd_iprintf(buffer, "PCM MIC ADC      : %iHz\n", val);
	}
	if ((ext & AC97_EI_SPDIF) || (ac97->flags & AC97_CS_SPDIF)) {
	        if (ac97->flags & AC97_CS_SPDIF)
			val = snd_ac97_read(ac97, AC97_CSR_SPDIF);
		else
			val = snd_ac97_read(ac97, AC97_SPDIF);

		snd_iprintf(buffer, "SPDIF Control    :%s%s%s%s Category=0x%x Generation=%i%s%s%s\n",
			val & AC97_SC_PRO ? " PRO" : " Consumer",
			val & AC97_SC_NAUDIO ? " Non-audio" : " PCM",
			val & AC97_SC_COPY ? "" : " Copyright",
			val & AC97_SC_PRE ? " Preemph50/15" : "",
			(val & AC97_SC_CC_MASK) >> AC97_SC_CC_SHIFT,
			(val & AC97_SC_L) >> 11,
			(ac97->flags & AC97_CS_SPDIF) ?
			    spdif_rates_cs4205[(val & AC97_SC_SPSR_MASK) >> AC97_SC_SPSR_SHIFT] :
			    spdif_rates[(val & AC97_SC_SPSR_MASK) >> AC97_SC_SPSR_SHIFT],
			(ac97->flags & AC97_CS_SPDIF) ?
			    (val & AC97_SC_DRS ? " Validity" : "") :
			    (val & AC97_SC_DRS ? " DRS" : ""),
			(ac97->flags & AC97_CS_SPDIF) ?
			    (val & AC97_SC_V ? " Enabled" : "") :
			    (val & AC97_SC_V ? " Validity" : ""));
		/* ALC650 specific*/
		if ((ac97->id & 0xfffffff0) == 0x414c4720 &&
		    (snd_ac97_read(ac97, AC97_ALC650_CLOCK) & 0x01)) {
			val = snd_ac97_read(ac97, AC97_ALC650_SPDIF_INPUT_STATUS2);
			if (val & AC97_ALC650_CLOCK_LOCK) {
				val = snd_ac97_read(ac97, AC97_ALC650_SPDIF_INPUT_STATUS1);
				snd_iprintf(buffer, "SPDIF In Status  :%s%s%s%s Category=0x%x Generation=%i",
					    val & AC97_ALC650_PRO ? " PRO" : " Consumer",
					    val & AC97_ALC650_NAUDIO ? " Non-audio" : " PCM",
					    val & AC97_ALC650_COPY ? "" : " Copyright",
					    val & AC97_ALC650_PRE ? " Preemph50/15" : "",
					    (val & AC97_ALC650_CC_MASK) >> AC97_ALC650_CC_SHIFT,
					    (val & AC97_ALC650_L) >> 15);
				val = snd_ac97_read(ac97, AC97_ALC650_SPDIF_INPUT_STATUS2);
				snd_iprintf(buffer, "%s Accuracy=%i%s%s\n",
					    spdif_rates[(val & AC97_ALC650_SPSR_MASK) >> AC97_ALC650_SPSR_SHIFT],
					    (val & AC97_ALC650_CLOCK_ACCURACY) >> AC97_ALC650_CLOCK_SHIFT,
					    (val & AC97_ALC650_CLOCK_LOCK ? " Locked" : " Unlocked"),
					    (val & AC97_ALC650_V ? " Validity?" : ""));
			} else {
				snd_iprintf(buffer, "SPDIF In Status  : Not Locked\n");
			}
		}
	}


      __modem:
	mext = snd_ac97_read(ac97, AC97_EXTENDED_MID);
	if (mext == 0)
		return;
	
	snd_iprintf(buffer, "Extended modem ID: codec=%i%s%s%s%s%s\n",
			(mext & AC97_MEI_ADDR_MASK) >> AC97_MEI_ADDR_SHIFT,
			mext & AC97_MEI_CID2 ? " CID2" : "",
			mext & AC97_MEI_CID1 ? " CID1" : "",
			mext & AC97_MEI_HANDSET ? " HSET" : "",
			mext & AC97_MEI_LINE2 ? " LIN2" : "",
			mext & AC97_MEI_LINE1 ? " LIN1" : "");
	val = snd_ac97_read(ac97, AC97_EXTENDED_MSTATUS);
	snd_iprintf(buffer, "Modem status     :%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
			val & AC97_MEA_GPIO ? " GPIO" : "",
			val & AC97_MEA_MREF ? " MREF" : "",
			val & AC97_MEA_ADC1 ? " ADC1" : "",
			val & AC97_MEA_DAC1 ? " DAC1" : "",
			val & AC97_MEA_ADC2 ? " ADC2" : "",
			val & AC97_MEA_DAC2 ? " DAC2" : "",
			val & AC97_MEA_HADC ? " HADC" : "",
			val & AC97_MEA_HDAC ? " HDAC" : "",
			val & AC97_MEA_PRA ? " PRA(GPIO)" : "",
			val & AC97_MEA_PRB ? " PRB(res)" : "",
			val & AC97_MEA_PRC ? " PRC(ADC1)" : "",
			val & AC97_MEA_PRD ? " PRD(DAC1)" : "",
			val & AC97_MEA_PRE ? " PRE(ADC2)" : "",
			val & AC97_MEA_PRF ? " PRF(DAC2)" : "",
			val & AC97_MEA_PRG ? " PRG(HADC)" : "",
			val & AC97_MEA_PRH ? " PRH(HDAC)" : "");
	if (mext & AC97_MEI_LINE1) {
		val = snd_ac97_read(ac97, AC97_LINE1_RATE);
		snd_iprintf(buffer, "Line1 rate       : %iHz\n", val);
	}
	if (mext & AC97_MEI_LINE2) {
		val = snd_ac97_read(ac97, AC97_LINE2_RATE);
		snd_iprintf(buffer, "Line2 rate       : %iHz\n", val);
	}
	if (mext & AC97_MEI_HANDSET) {
		val = snd_ac97_read(ac97, AC97_HANDSET_RATE);
		snd_iprintf(buffer, "Headset rate     : %iHz\n", val);
	}
}

static void snd_ac97_proc_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	ac97_t *ac97 = snd_magic_cast(ac97_t, entry->private_data, return);
	
	if ((ac97->id & 0xffffff40) == AC97_ID_AD1881) {	// Analog Devices AD1881/85/86
		int idx;
		down(&ac97->spec.ad18xx.mutex);
		for (idx = 0; idx < 3; idx++)
			if (ac97->spec.ad18xx.id[idx]) {
				/* select single codec */
				snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, ac97->spec.ad18xx.unchained[idx] | ac97->spec.ad18xx.chained[idx]);
				snd_ac97_proc_read_main(ac97, buffer, idx);
				snd_iprintf(buffer, "\n\n");
			}
		/* select all codecs */
		snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, 0x7000);
		up(&ac97->spec.ad18xx.mutex);
		
		snd_iprintf(buffer, "\nAD18XX configuration\n");
		snd_iprintf(buffer, "Unchained        : 0x%04x,0x%04x,0x%04x\n",
			ac97->spec.ad18xx.unchained[0],
			ac97->spec.ad18xx.unchained[1],
			ac97->spec.ad18xx.unchained[2]);
		snd_iprintf(buffer, "Chained          : 0x%04x,0x%04x,0x%04x\n",
			ac97->spec.ad18xx.chained[0],
			ac97->spec.ad18xx.chained[1],
			ac97->spec.ad18xx.chained[2]);
	} else {
		snd_ac97_proc_read_main(ac97, buffer, 0);
	}
}

static void snd_ac97_proc_regs_read_main(ac97_t *ac97, snd_info_buffer_t * buffer, int subidx)
{
	int reg, val;

	for (reg = 0; reg < 0x80; reg += 2) {
		val = snd_ac97_read(ac97, reg);
		snd_iprintf(buffer, "%i:%02x = %04x\n", subidx, reg, val);
	}
}

static void snd_ac97_proc_regs_read(snd_info_entry_t *entry, 
				    snd_info_buffer_t * buffer)
{
	ac97_t *ac97 = snd_magic_cast(ac97_t, entry->private_data, return);

	if ((ac97->id & 0xffffff40) == AC97_ID_AD1881) {	// Analog Devices AD1881/85/86

		int idx;
		down(&ac97->spec.ad18xx.mutex);
		for (idx = 0; idx < 3; idx++)
			if (ac97->spec.ad18xx.id[idx]) {
				/* select single codec */
				snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, ac97->spec.ad18xx.unchained[idx] | ac97->spec.ad18xx.chained[idx]);
				snd_ac97_proc_regs_read_main(ac97, buffer, idx);
			}
		/* select all codecs */
		snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, 0x7000);
		up(&ac97->spec.ad18xx.mutex);
	} else {
		snd_ac97_proc_regs_read_main(ac97, buffer, 0);
	}	
}

void snd_ac97_proc_init(snd_card_t * card, ac97_t * ac97, const char *prefix)
{
	snd_info_entry_t *entry;
	char name[32];

	if (ac97->num)
		sprintf(name, "%s#%d-%d", prefix, ac97->addr, ac97->num);
	else
		sprintf(name, "%s#%d", prefix, ac97->addr);
	if (! snd_card_proc_new(card, name, &entry))
		snd_info_set_text_ops(entry, ac97, snd_ac97_proc_read);
	if (ac97->num)
		sprintf(name, "%s#%d-%dregs", prefix, ac97->addr, ac97->num);
	else
		sprintf(name, "%s#%dregs", prefix, ac97->addr);
	if (! snd_card_proc_new(card, name, &entry))
		snd_info_set_text_ops(entry, ac97, snd_ac97_proc_regs_read);
}
