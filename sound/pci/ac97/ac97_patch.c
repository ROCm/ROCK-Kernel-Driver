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
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/asoundef.h>
#include <sound/initval.h>
#include "ac97_patch.h"

/*
 *  Chip specific initialization
 */

int patch_yamaha_ymf753(ac97_t * ac97)
{
	/* Patch for Yamaha YMF753, Copyright (c) by David Shust, dshust@shustring.com.
	   This chip has nonstandard and extended behaviour with regard to its S/PDIF output.
	   The AC'97 spec states that the S/PDIF signal is to be output at pin 48.
	   The YMF753 will ouput the S/PDIF signal to pin 43, 47 (EAPD), or 48.
	   By default, no output pin is selected, and the S/PDIF signal is not output.
	   There is also a bit to mute S/PDIF output in a vendor-specific register.
	*/
	ac97->caps |= AC97_BC_BASS_TREBLE;
	ac97->caps |= 0x04 << 10; /* Yamaha 3D enhancement */
	return 0;
}

/*
 * May 2, 2003 Liam Girdwood <liam.girdwood@wolfsonmicro.com>
 *  removed broken wolfson00 patch.
 *  added support for WM9705,WM9708,WM9709,WM9710,WM9711,WM9712 and WM9717.
 */

#define AC97_WM97XX_FMIXER_VOL	0x72
#define AC97_WM9704_RMIXER_VOL	0x74
#define AC97_WM9704_TEST	0x5a
#define AC97_WM9704_RPCM_VOL	0x70
#define AC97_WM9711_OUT3VOL	0x16
 
int patch_wolfson03(ac97_t * ac97)
{
	/* This is known to work for the ViewSonic ViewPad 1000
	   Randolph Bentson <bentson@holmsjoen.com> */

	// WM9703/9707/9708/9717
	snd_ac97_write_cache(ac97, AC97_WM97XX_FMIXER_VOL, 0x0808);
	snd_ac97_write_cache(ac97, AC97_GENERAL_PURPOSE, 0x8000);
	return 0;
}
  
int patch_wolfson04(ac97_t * ac97)
{
	// WM9704M/9704Q
	// set front and rear mixer volume
	snd_ac97_write_cache(ac97, AC97_WM97XX_FMIXER_VOL, 0x0808);
	snd_ac97_write_cache(ac97, AC97_WM9704_RMIXER_VOL, 0x0808);
	
	// patch for DVD noise
	snd_ac97_write_cache(ac97, AC97_WM9704_TEST, 0x0200);
 
	// init vol
	snd_ac97_write_cache(ac97, AC97_WM9704_RPCM_VOL, 0x0808);
 
	// set rear surround volume
	snd_ac97_write_cache(ac97, AC97_SURROUND_MASTER, 0x0000);
	return 0;
}
  
int patch_wolfson05(ac97_t * ac97)
{
	// WM9705, WM9710
	// set front mixer volume
	snd_ac97_write_cache(ac97, AC97_WM97XX_FMIXER_VOL, 0x0808);
	return 0;
}

int patch_wolfson11(ac97_t * ac97)
{
	// WM9711, WM9712
	// set out3 volume
	snd_ac97_write_cache(ac97, AC97_WM9711_OUT3VOL, 0x0808);
	return 0;
}

int patch_tritech_tr28028(ac97_t * ac97)
{
	snd_ac97_write_cache(ac97, 0x26, 0x0300);
	snd_ac97_write_cache(ac97, 0x26, 0x0000);
	snd_ac97_write_cache(ac97, AC97_SURROUND_MASTER, 0x0000);
	snd_ac97_write_cache(ac97, AC97_SPDIF, 0x0000);
	return 0;
}

int patch_sigmatel_stac9708(ac97_t * ac97)
{
	unsigned int codec72, codec6c;

	codec72 = snd_ac97_read(ac97, AC97_SIGMATEL_BIAS2) & 0x8000;
	codec6c = snd_ac97_read(ac97, AC97_SIGMATEL_ANALOG);

	if ((codec72==0) && (codec6c==0)) {
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x1000);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0007);
	} else if ((codec72==0x8000) && (codec6c==0)) {
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x1001);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_DAC2INVERT, 0x0008);
	} else if ((codec72==0x8000) && (codec6c==0x0080)) {
		/* nothing */
	}
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

int patch_sigmatel_stac9721(ac97_t * ac97)
{
	if (snd_ac97_read(ac97, AC97_SIGMATEL_ANALOG) == 0) {
		// patch for SigmaTel
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x4000);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0002);
	}
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

int patch_sigmatel_stac9744(ac97_t * ac97)
{
	// patch for SigmaTel
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x0000);	/* is this correct? --jk */
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0002);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

int patch_sigmatel_stac9756(ac97_t * ac97)
{
	// patch for SigmaTel
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x0000);	/* is this correct? --jk */
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0002);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

int patch_cirrus_spdif(ac97_t * ac97)
{
	/* Basically, the cs4201/cs4205/cs4297a has non-standard sp/dif registers.
	   WHY CAN'T ANYONE FOLLOW THE BLOODY SPEC?  *sigh*
	   - sp/dif EA ID is not set, but sp/dif is always present.
	   - enable/disable is spdif register bit 15.
	   - sp/dif control register is 0x68.  differs from AC97:
	   - valid is bit 14 (vs 15)
	   - no DRS
	   - only 44.1/48k [00 = 48, 01=44,1] (AC97 is 00=44.1, 10=48)
	   - sp/dif ssource select is in 0x5e bits 0,1.
	*/

	ac97->flags |= AC97_CS_SPDIF; 
	ac97->rates[AC97_RATES_SPDIF] &= ~SNDRV_PCM_RATE_32000;
        ac97->ext_id |= AC97_EI_SPDIF;	/* force the detection of spdif */
	snd_ac97_write_cache(ac97, AC97_CSR_ACMODE, 0x0080);
	return 0;
}

int patch_cirrus_cs4299(ac97_t * ac97)
{
	/* force the detection of PC Beep */
	ac97->flags |= AC97_HAS_PC_BEEP;
	
	return patch_cirrus_spdif(ac97);
}

int patch_conexant(ac97_t * ac97)
{
	ac97->flags |= AC97_CX_SPDIF;
        ac97->ext_id |= AC97_EI_SPDIF;	/* force the detection of spdif */
	return 0;
}

int patch_ad1819(ac97_t * ac97)
{
	// patch for Analog Devices
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, 0x7000); /* select all codecs */
	return 0;
}

static unsigned short patch_ad1881_unchained(ac97_t * ac97, int idx, unsigned short mask)
{
	unsigned short val;

	// test for unchained codec
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, mask);
	snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0000);	/* ID0C, ID1C, SDIE = off */
	val = snd_ac97_read(ac97, AC97_VENDOR_ID2);
	if ((val & 0xff40) != 0x5340)
		return 0;
	ac97->spec.ad18xx.unchained[idx] = mask;
	ac97->spec.ad18xx.id[idx] = val;
	ac97->spec.ad18xx.codec_cfg[idx] = 0x0000;
	return mask;
}

static int patch_ad1881_chained1(ac97_t * ac97, int idx, unsigned short codec_bits)
{
	static int cfg_bits[3] = { 1<<12, 1<<14, 1<<13 };
	unsigned short val;
	
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, cfg_bits[idx]);
	snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0004);	// SDIE
	val = snd_ac97_read(ac97, AC97_VENDOR_ID2);
	if ((val & 0xff40) != 0x5340)
		return 0;
	if (codec_bits)
		snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, codec_bits);
	ac97->spec.ad18xx.chained[idx] = cfg_bits[idx];
	ac97->spec.ad18xx.id[idx] = val;
	ac97->spec.ad18xx.codec_cfg[idx] = codec_bits ? codec_bits : 0x0004;
	return 1;
}

static void patch_ad1881_chained(ac97_t * ac97, int unchained_idx, int cidx1, int cidx2)
{
	// already detected?
	if (ac97->spec.ad18xx.unchained[cidx1] || ac97->spec.ad18xx.chained[cidx1])
		cidx1 = -1;
	if (ac97->spec.ad18xx.unchained[cidx2] || ac97->spec.ad18xx.chained[cidx2])
		cidx2 = -1;
	if (cidx1 < 0 && cidx2 < 0)
		return;
	// test for chained codecs
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, ac97->spec.ad18xx.unchained[unchained_idx]);
	snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0002);		// ID1C
	ac97->spec.ad18xx.codec_cfg[unchained_idx] = 0x0002;
	if (cidx1 >= 0) {
		if (patch_ad1881_chained1(ac97, cidx1, 0x0006))		// SDIE | ID1C
			patch_ad1881_chained1(ac97, cidx2, 0);
		else if (patch_ad1881_chained1(ac97, cidx2, 0x0006))	// SDIE | ID1C
			patch_ad1881_chained1(ac97, cidx1, 0);
	} else if (cidx2 >= 0) {
		patch_ad1881_chained1(ac97, cidx2, 0);
	}
}

int patch_ad1881(ac97_t * ac97)
{
	static const char cfg_idxs[3][2] = {
		{2, 1},
		{0, 2},
		{0, 1}
	};
	
	// patch for Analog Devices
	unsigned short codecs[3];
	int idx, num;

	init_MUTEX(&ac97->spec.ad18xx.mutex);

	codecs[0] = patch_ad1881_unchained(ac97, 0, (1<<12));
	codecs[1] = patch_ad1881_unchained(ac97, 1, (1<<14));
	codecs[2] = patch_ad1881_unchained(ac97, 2, (1<<13));

	snd_runtime_check(codecs[0] | codecs[1] | codecs[2], goto __end);

	for (idx = 0; idx < 3; idx++)
		if (ac97->spec.ad18xx.unchained[idx])
			patch_ad1881_chained(ac97, idx, cfg_idxs[idx][0], cfg_idxs[idx][1]);

	if (ac97->spec.ad18xx.id[1]) {
		ac97->flags |= AC97_AD_MULTI;
		ac97->scaps |= AC97_SCAP_SURROUND_DAC;
	}
	if (ac97->spec.ad18xx.id[2]) {
		ac97->flags |= AC97_AD_MULTI;
		ac97->scaps |= AC97_SCAP_CENTER_LFE_DAC;
	}

      __end:
	/* select all codecs */
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, 0x7000);
	/* check if only one codec is present */
	for (idx = num = 0; idx < 3; idx++)
		if (ac97->spec.ad18xx.id[idx])
			num++;
	if (num == 1) {
		/* ok, deselect all ID bits */
		snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0000);
		ac97->spec.ad18xx.codec_cfg[0] = 
			ac97->spec.ad18xx.codec_cfg[1] = 
			ac97->spec.ad18xx.codec_cfg[2] = 0x0000;
	}
	/* required for AD1886/AD1885 combination */
	ac97->ext_id = snd_ac97_read(ac97, AC97_EXTENDED_ID);
	if (ac97->spec.ad18xx.id[0]) {
		ac97->id &= 0xffff0000;
		ac97->id |= ac97->spec.ad18xx.id[0];
	}
	return 0;
}

int patch_ad1885(ac97_t * ac97)
{
	unsigned short jack;

	patch_ad1881(ac97);
	/* This is required to deal with the Intel D815EEAL2 */
	/* i.e. Line out is actually headphone out from codec */

	/* turn off jack sense bits D8 & D9 */
	jack = snd_ac97_read(ac97, AC97_AD_JACK_SPDIF);
	snd_ac97_write_cache(ac97, AC97_AD_JACK_SPDIF, jack | 0x0300);
	return 0;
}

int patch_ad1886(ac97_t * ac97)
{
	patch_ad1881(ac97);
	/* Presario700 workaround */
	/* for Jack Sense/SPDIF Register misetting causing */
	snd_ac97_write_cache(ac97, AC97_AD_JACK_SPDIF, 0x0010);
	return 0;
}

int patch_ad1980(ac97_t * ac97)
{
	unsigned short misc;
	
	patch_ad1881(ac97);
	/* Switch FRONT/SURROUND LINE-OUT/HP-OUT default connection */
	/* it seems that most vendors connect line-out connector to headphone out of AC'97 */
	misc = snd_ac97_read(ac97, AC97_AD_MISC);
	snd_ac97_write_cache(ac97, AC97_AD_MISC, misc | 0x0420);
	return 0;
}

int patch_alc650(ac97_t * ac97)
{
	unsigned short val, nval;
	int spdif = 0;

	val = snd_ac97_read(ac97, AC97_ALC650_MULTICH);
	snd_ac97_write(ac97, AC97_ALC650_MULTICH, val ^ 0x80);
	nval = snd_ac97_read(ac97, AC97_ALC650_MULTICH);
	ac97->spec.dev_flags = 0;
	if (val != nval) {
		ac97->spec.dev_flags = 1; /* rev.E or later */
		snd_ac97_write(ac97, AC97_ALC650_MULTICH, val); /* push back */
	}

	/* check spdif */
	if (ac97->spec.dev_flags) {
		val = snd_ac97_read(ac97, AC97_EXTENDED_STATUS);
		if (val & AC97_EA_SPCV)
			spdif = 1;
	}
	if (spdif) {
		/* enable spdif in */
		snd_ac97_write_cache(ac97, AC97_ALC650_CLOCK,
				     snd_ac97_read(ac97, AC97_ALC650_CLOCK) | 0x03);
	} else
		ac97->ext_id &= ~AC97_EI_SPDIF; /* disable extended-id */

	val = snd_ac97_read(ac97, AC97_ALC650_MULTICH);
	val &= ~0xc000; /* slot: 3,4,7,8,6,9 */
	snd_ac97_write_cache(ac97, AC97_ALC650_MULTICH, val);

	if (! ac97->spec.dev_flags) {
		/* set GPIO */
		int mic_off;
		mic_off = snd_ac97_read(ac97, AC97_ALC650_MULTICH) & (1 << 10);
		/* GPIO0 direction */
		val = snd_ac97_read(ac97, 0x76);
		if (mic_off)
			val &= ~0x01;
		else
			val |= 0x01;
		snd_ac97_write_cache(ac97, 0x76, val);
		val = snd_ac97_read(ac97, 0x78);
		if (mic_off)
			val &= ~0x100;
		else
			val = val | 0x100;
		snd_ac97_write_cache(ac97, 0x78, val);
	}

	/* full DAC volume */
	snd_ac97_write_cache(ac97, AC97_ALC650_SURR_DAC_VOL, 0x0808);
	snd_ac97_write_cache(ac97, AC97_ALC650_LFE_DAC_VOL, 0x0808);
	return 0;
}

int patch_cm9739(ac97_t * ac97)
{
	unsigned short val;

	/* check spdif */
	val = snd_ac97_read(ac97, AC97_EXTENDED_STATUS);
	if (val & AC97_EA_SPCV) {
		/* enable spdif in */
		snd_ac97_write_cache(ac97, AC97_CM9739_SPDIF_CTRL,
				     snd_ac97_read(ac97, AC97_CM9739_SPDIF_CTRL) | 0x01);
	} else {
		ac97->ext_id &= ~AC97_EI_SPDIF; /* disable extended-id */
	}

	/* set-up multi channel */
	/* bit 13: enable internal vref output for mic */
	/* bit 12: enable center/lfe */
	/* bit 14: 0 = SPDIF, 1 = EAPD */
	snd_ac97_write_cache(ac97, AC97_CM9739_MULTI_CHAN, 0x3000);

	/* FIXME: set up GPIO */
	snd_ac97_write_cache(ac97, 0x70, 0x0100);
	snd_ac97_write_cache(ac97, 0x72, 0x0020);

	return 0;
}

