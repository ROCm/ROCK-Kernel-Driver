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
#include <linux/pci.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/asoundef.h>
#include <sound/initval.h>
#include "ac97_local.h"
#include "ac97_id.h"
#include "ac97_patch.h"

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Universal interface for Audio Codec '97");
MODULE_LICENSE("GPL");

static int enable_loopback;

module_param(enable_loopback, bool, 0444);
MODULE_PARM_DESC(enable_loopback, "Enable AC97 ADC/DAC Loopback Control");
MODULE_PARM_SYNTAX(enable_loopback, SNDRV_BOOLEAN_FALSE_DESC);

#define chip_t ac97_t

/*

 */

typedef struct {
	unsigned int id;
	unsigned int mask;
	const char *name;
	int (*patch)(ac97_t *ac97);
	int (*mpatch)(ac97_t *ac97);
} ac97_codec_id_t;

static const ac97_codec_id_t snd_ac97_codec_id_vendors[] = {
{ 0x414b4d00, 0xffffff00, "Asahi Kasei",	NULL,	NULL },
{ 0x41445300, 0xffffff00, "Analog Devices",	NULL,	NULL },
{ 0x414c4300, 0xffffff00, "Realtek",		NULL,	NULL },
{ 0x414c4700, 0xffffff00, "Realtek",		NULL,	NULL },
{ 0x434d4900, 0xffffff00, "C-Media Electronics", NULL,	NULL },
{ 0x43525900, 0xffffff00, "Cirrus Logic",	NULL,	NULL },
{ 0x43585400, 0xffffff00, "Conexant",           NULL,	NULL },
{ 0x44543000, 0xffffff00, "Diamond Technology", NULL,	NULL },
{ 0x454d4300, 0xffffff00, "eMicro",		NULL,	NULL },
{ 0x45838300, 0xffffff00, "ESS Technology",	NULL,	NULL },
{ 0x48525300, 0xffffff00, "Intersil",		NULL,	NULL },
{ 0x49434500, 0xffffff00, "ICEnsemble",		NULL,	NULL },
{ 0x49544500, 0xffffff00, "ITE Tech.Inc",	NULL,	NULL },
{ 0x4e534300, 0xffffff00, "National Semiconductor", NULL, NULL },
{ 0x50534300, 0xffffff00, "Philips",		NULL,	NULL },
{ 0x53494c00, 0xffffff00, "Silicon Laboratory",	NULL,	NULL },
{ 0x54524100, 0xffffff00, "TriTech",		NULL,	NULL },
{ 0x54584e00, 0xffffff00, "Texas Instruments",	NULL,	NULL },
{ 0x56494100, 0xffffff00, "VIA Technologies",   NULL,	NULL },
{ 0x57454300, 0xffffff00, "Winbond",		NULL,	NULL },
{ 0x574d4c00, 0xffffff00, "Wolfson",		NULL,	NULL },
{ 0x594d4800, 0xffffff00, "Yamaha",		NULL,	NULL },
{ 0x83847600, 0xffffff00, "SigmaTel",		NULL,	NULL },
{ 0,	      0, 	  NULL,			NULL,	NULL }
};

static const ac97_codec_id_t snd_ac97_codec_ids[] = {
{ 0x414b4d00, 0xffffffff, "AK4540",		NULL,		NULL },
{ 0x414b4d01, 0xffffffff, "AK4542",		NULL,		NULL },
{ 0x414b4d02, 0xffffffff, "AK4543",		NULL,		NULL },
{ 0x414b4d06, 0xffffffff, "AK4544A",		NULL,		NULL },
{ 0x414b4d07, 0xffffffff, "AK4545",		NULL,		NULL },
{ 0x41445303, 0xffffffff, "AD1819",		patch_ad1819,	NULL },
{ 0x41445340, 0xffffffff, "AD1881",		patch_ad1881,	NULL },
{ 0x41445348, 0xffffffff, "AD1881A",		patch_ad1881,	NULL },
{ 0x41445360, 0xffffffff, "AD1885",		patch_ad1885,	NULL },
{ 0x41445361, 0xffffffff, "AD1886",		patch_ad1886,	NULL },
{ 0x41445362, 0xffffffff, "AD1887",		patch_ad1881,	NULL },
{ 0x41445363, 0xffffffff, "AD1886A",		patch_ad1881,	NULL },
{ 0x41445368, 0xffffffff, "AD1888",		patch_ad1888,	NULL },
{ 0x41445370, 0xffffffff, "AD1980",		patch_ad1980,	NULL },
{ 0x41445372, 0xffffffff, "AD1981A",		patch_ad1981a,	NULL },
{ 0x41445374, 0xffffffff, "AD1981B",		patch_ad1981b,	NULL },
{ 0x41445375, 0xffffffff, "AD1985",		patch_ad1985,	NULL },
{ 0x414c4300, 0xffffff00, "ALC100/100P", 	NULL,		NULL },
{ 0x414c4710, 0xfffffff0, "ALC200/200P",	NULL,		NULL },
{ 0x414c4721, 0xffffffff, "ALC650D",		NULL,	NULL }, /* already patched */
{ 0x414c4722, 0xffffffff, "ALC650E",		NULL,	NULL }, /* already patched */
{ 0x414c4723, 0xffffffff, "ALC650F",		NULL,	NULL }, /* already patched */
{ 0x414c4720, 0xfffffff0, "ALC650",		patch_alc650,	NULL },
{ 0x414c4760, 0xfffffff0, "ALC655",		patch_alc655,	NULL },
{ 0x414c4780, 0xfffffff0, "ALC658",		patch_alc655,	NULL },
{ 0x414c4790, 0xfffffff0, "ALC850",		patch_alc850,	NULL },
{ 0x414c4730, 0xffffffff, "ALC101",		NULL,		NULL },
{ 0x414c4740, 0xfffffff0, "ALC202",		NULL,		NULL },
{ 0x414c4750, 0xfffffff0, "ALC250",		NULL,		NULL },
{ 0x414c4770, 0xfffffff0, "ALC203",		NULL,		NULL },
{ 0x434d4941, 0xffffffff, "CMI9738",		patch_cm9738,	NULL },
{ 0x434d4961, 0xffffffff, "CMI9739",		patch_cm9739,	NULL },
{ 0x43525900, 0xfffffff8, "CS4297",		NULL,		NULL },
{ 0x43525910, 0xfffffff8, "CS4297A",		patch_cirrus_spdif,	NULL },
{ 0x43525920, 0xfffffff8, "CS4294/4298",	NULL,		NULL },
{ 0x43525928, 0xfffffff8, "CS4294",		NULL,		NULL },
{ 0x43525930, 0xfffffff8, "CS4299",		patch_cirrus_cs4299,	NULL },
{ 0x43525948, 0xfffffff8, "CS4201",		NULL,		NULL },
{ 0x43525958, 0xfffffff8, "CS4205",		patch_cirrus_spdif,	NULL },
{ 0x43525960, 0xfffffff8, "CS4291",		NULL,		NULL },
{ 0x43525970, 0xfffffff8, "CS4202",		NULL,		NULL },
{ 0x43585421, 0xffffffff, "HSD11246",		NULL,		NULL },	// SmartMC II
{ 0x43585428, 0xfffffff8, "Cx20468",		patch_conexant,	NULL }, // SmartAMC fixme: the mask might be different
{ 0x44543031, 0xfffffff0, "DT0398",		NULL,		NULL },
{ 0x454d4328, 0xffffffff, "28028",		NULL,		NULL },  // same as TR28028?
{ 0x45838308, 0xffffffff, "ESS1988",		NULL,		NULL },
{ 0x48525300, 0xffffff00, "HMP9701",		NULL,		NULL },
{ 0x49434501, 0xffffffff, "ICE1230",		NULL,		NULL },
{ 0x49434511, 0xffffffff, "ICE1232",		NULL,		NULL }, // alias VIA VT1611A?
{ 0x49434514, 0xffffffff, "ICE1232A",		NULL,		NULL },
{ 0x49434551, 0xffffffff, "VT1616", 		patch_vt1616,	NULL }, 
{ 0x49434552, 0xffffffff, "VT1616i",		patch_vt1616,	NULL }, // VT1616 compatible (chipset integrated)
{ 0x49544520, 0xffffffff, "IT2226E",		NULL,		NULL },
{ 0x49544561, 0xffffffff, "IT2646E",		patch_it2646,	NULL },
{ 0x4e534300, 0xffffffff, "LM4540/43/45/46/48",	NULL,		NULL }, // only guess --jk
{ 0x4e534331, 0xffffffff, "LM4549",		NULL,		NULL },
{ 0x4e534350, 0xffffffff, "LM4550",		NULL,		NULL },
{ 0x50534304, 0xffffffff, "UCB1400",		NULL,		NULL },
{ 0x53494c20, 0xffffffe0, "Si3036/8",		NULL,		mpatch_si3036 },
{ 0x54524102, 0xffffffff, "TR28022",		NULL,		NULL },
{ 0x54524106, 0xffffffff, "TR28026",		NULL,		NULL },
{ 0x54524108, 0xffffffff, "TR28028",		patch_tritech_tr28028,	NULL }, // added by xin jin [07/09/99]
{ 0x54524123, 0xffffffff, "TR28602",		NULL,		NULL }, // only guess --jk [TR28023 = eMicro EM28023 (new CT1297)]
{ 0x54584e20, 0xffffffff, "TLC320AD9xC",	NULL,		NULL },
{ 0x56494161, 0xffffffff, "VIA1612A",		NULL,		NULL }, // modified ICE1232 with S/PDIF
{ 0x57454301, 0xffffffff, "W83971D",		NULL,		NULL },
{ 0x574d4c00, 0xffffffff, "WM9701A",		NULL,		NULL },
{ 0x574d4C03, 0xffffffff, "WM9703/WM9707/WM9708/WM9717", patch_wolfson03, NULL},
{ 0x574d4C04, 0xffffffff, "WM9704M/WM9704Q",	patch_wolfson04, NULL},
{ 0x574d4C05, 0xffffffff, "WM9705/WM9710",	patch_wolfson05, NULL},
{ 0x574d4C09, 0xffffffff, "WM9709",		NULL,		NULL},
{ 0x574d4C12, 0xffffffff, "WM9711/WM9712",	patch_wolfson11, NULL},
{ 0x594d4800, 0xffffffff, "YMF743",		NULL,		NULL },
{ 0x594d4802, 0xffffffff, "YMF752",		NULL,		NULL },
{ 0x594d4803, 0xffffffff, "YMF753",		patch_yamaha_ymf753,	NULL },
{ 0x83847600, 0xffffffff, "STAC9700/83/84",	patch_sigmatel_stac9700,	NULL },
{ 0x83847604, 0xffffffff, "STAC9701/3/4/5",	NULL,		NULL },
{ 0x83847605, 0xffffffff, "STAC9704",		NULL,		NULL },
{ 0x83847608, 0xffffffff, "STAC9708/11",	patch_sigmatel_stac9708,	NULL },
{ 0x83847609, 0xffffffff, "STAC9721/23",	patch_sigmatel_stac9721,	NULL },
{ 0x83847644, 0xffffffff, "STAC9744",		patch_sigmatel_stac9744,	NULL },
{ 0x83847650, 0xffffffff, "STAC9750/51",	NULL,		NULL },	// patch?
{ 0x83847652, 0xffffffff, "STAC9752/53",	NULL,		NULL }, // patch?
{ 0x83847656, 0xffffffff, "STAC9756/57",	patch_sigmatel_stac9756,	NULL },
{ 0x83847658, 0xffffffff, "STAC9758/59",	patch_sigmatel_stac9758,	NULL },
{ 0x83847666, 0xffffffff, "STAC9766/67",	NULL,		NULL }, // patch?
{ 0, 	      0,	  NULL,			NULL,		NULL }
};

const char *snd_ac97_stereo_enhancements[] =
{
  /*   0 */ "No 3D Stereo Enhancement",
  /*   1 */ "Analog Devices Phat Stereo",
  /*   2 */ "Creative Stereo Enhancement",
  /*   3 */ "National Semi 3D Stereo Enhancement",
  /*   4 */ "YAMAHA Ymersion",
  /*   5 */ "BBE 3D Stereo Enhancement",
  /*   6 */ "Crystal Semi 3D Stereo Enhancement",
  /*   7 */ "Qsound QXpander",
  /*   8 */ "Spatializer 3D Stereo Enhancement",
  /*   9 */ "SRS 3D Stereo Enhancement",
  /*  10 */ "Platform Tech 3D Stereo Enhancement",
  /*  11 */ "AKM 3D Audio",
  /*  12 */ "Aureal Stereo Enhancement",
  /*  13 */ "Aztech 3D Enhancement",
  /*  14 */ "Binaura 3D Audio Enhancement",
  /*  15 */ "ESS Technology Stereo Enhancement",
  /*  16 */ "Harman International VMAx",
  /*  17 */ "Nvidea/IC Ensemble/KS Waves 3D Stereo Enhancement",
  /*  18 */ "Philips Incredible Sound",
  /*  19 */ "Texas Instruments 3D Stereo Enhancement",
  /*  20 */ "VLSI Technology 3D Stereo Enhancement",
  /*  21 */ "TriTech 3D Stereo Enhancement",
  /*  22 */ "Realtek 3D Stereo Enhancement",
  /*  23 */ "Samsung 3D Stereo Enhancement",
  /*  24 */ "Wolfson Microelectronics 3D Enhancement",
  /*  25 */ "Delta Integration 3D Enhancement",
  /*  26 */ "SigmaTel 3D Enhancement",
  /*  27 */ "IC Ensemble/KS Waves",
  /*  28 */ "Rockwell 3D Stereo Enhancement",
  /*  29 */ "Reserved 29",
  /*  30 */ "Reserved 30",
  /*  31 */ "Reserved 31"
};

/*
 *  I/O routines
 */

static int snd_ac97_valid_reg(ac97_t *ac97, unsigned short reg)
{
	if (ac97->limited_regs && ! test_bit(reg, ac97->reg_accessed))
  		return 0;

	/* filter some registers for buggy codecs */
	switch (ac97->id) {
	case AC97_ID_AK4540:
	case AC97_ID_AK4542:
		if (reg <= 0x1c || reg == 0x20 || reg == 0x26 || reg >= 0x7c)
			return 1;
		return 0;
	case AC97_ID_AD1819:	/* AD1819 */
	case AC97_ID_AD1881:	/* AD1881 */
	case AC97_ID_AD1881A:	/* AD1881A */
		if (reg >= 0x3a && reg <= 0x6e)	/* 0x59 */
			return 0;
		return 1;
	case AC97_ID_AD1885:	/* AD1885 */
	case AC97_ID_AD1886:	/* AD1886 */
	case AC97_ID_AD1886A:	/* AD1886A - !!verify!! --jk */
	case AC97_ID_AD1887:	/* AD1887 - !!verify!! --jk */
		if (reg == 0x5a)
			return 1;
		if (reg >= 0x3c && reg <= 0x6e)	/* 0x59 */
			return 0;
		return 1;
	case AC97_ID_STAC9700:
	case AC97_ID_STAC9704:
	case AC97_ID_STAC9705:
	case AC97_ID_STAC9708:
	case AC97_ID_STAC9721:
	case AC97_ID_STAC9744:
	case AC97_ID_STAC9756:
		if (reg <= 0x3a || reg >= 0x5a)
			return 1;
		return 0;
	}
	return 1;
}

/**
 * snd_ac97_write - write a value on the given register
 * @ac97: the ac97 instance
 * @reg: the register to change
 * @value: the value to set
 *
 * Writes a value on the given register.  This will invoke the write
 * callback directly after the register check.
 * This function doesn't change the register cache unlike
 * #snd_ca97_write_cache(), so use this only when you don't want to
 * reflect the change to the suspend/resume state.
 */
void snd_ac97_write(ac97_t *ac97, unsigned short reg, unsigned short value)
{
	if (!snd_ac97_valid_reg(ac97, reg))
		return;
	if ((ac97->id & 0xffffff00) == AC97_ID_ALC100) {
		/* Fix H/W bug of ALC100/100P */
		if (reg == AC97_MASTER || reg == AC97_HEADPHONE)
			ac97->bus->write(ac97, AC97_RESET, 0);	/* reset audio codec */
	}
	ac97->bus->write(ac97, reg, value);
}

/**
 * snd_ac97_read - read a value from the given register
 * 
 * @ac97: the ac97 instance
 * @reg: the register to read
 *
 * Reads a value from the given register.  This will invoke the read
 * callback directly after the register check.
 *
 * Returns the read value.
 */
unsigned short snd_ac97_read(ac97_t *ac97, unsigned short reg)
{
	if (!snd_ac97_valid_reg(ac97, reg))
		return 0;
	return ac97->bus->read(ac97, reg);
}

/* read a register - return the cached value if already read */
static inline unsigned short snd_ac97_read_cache(ac97_t *ac97, unsigned short reg)
{
	if (! test_bit(reg, ac97->reg_accessed)) {
		ac97->regs[reg] = ac97->bus->read(ac97, reg);
		// set_bit(reg, ac97->reg_accessed);
	}
	return ac97->regs[reg];
}

/**
 * snd_ac97_write_cache - write a value on the given register and update the cache
 * @ac97: the ac97 instance
 * @reg: the register to change
 * @value: the value to set
 *
 * Writes a value on the given register and updates the register
 * cache.  The cached values are used for the cached-read and the
 * suspend/resume.
 */
void snd_ac97_write_cache(ac97_t *ac97, unsigned short reg, unsigned short value)
{
	if (!snd_ac97_valid_reg(ac97, reg))
		return;
	spin_lock(&ac97->reg_lock);
	ac97->regs[reg] = value;
	spin_unlock(&ac97->reg_lock);
	ac97->bus->write(ac97, reg, value);
	set_bit(reg, ac97->reg_accessed);
}

/**
 * snd_ac97_update - update the value on the given register
 * @ac97: the ac97 instance
 * @reg: the register to change
 * @value: the value to set
 *
 * Compares the value with the register cache and updates the value
 * only when the value is changed.
 *
 * Returns 1 if the value is changed, 0 if no change, or a negative
 * code on failure.
 */
int snd_ac97_update(ac97_t *ac97, unsigned short reg, unsigned short value)
{
	int change;

	if (!snd_ac97_valid_reg(ac97, reg))
		return -EINVAL;
	spin_lock(&ac97->reg_lock);
	change = ac97->regs[reg] != value;
	if (change) {
		ac97->regs[reg] = value;
		spin_unlock(&ac97->reg_lock);
		ac97->bus->write(ac97, reg, value);
	} else
		spin_unlock(&ac97->reg_lock);
	return change;
}

/**
 * snd_ac97_update_bits - update the bits on the given register
 * @ac97: the ac97 instance
 * @reg: the register to change
 * @mask: the bit-mask to change
 * @value: the value to set
 *
 * Updates the masked-bits on the given register only when the value
 * is changed.
 *
 * Returns 1 if the bits are changed, 0 if no change, or a negative
 * code on failure.
 */
int snd_ac97_update_bits(ac97_t *ac97, unsigned short reg, unsigned short mask, unsigned short value)
{
	int change;
	unsigned short old, new;

	if (!snd_ac97_valid_reg(ac97, reg))
		return -EINVAL;
	spin_lock(&ac97->reg_lock);
	old = snd_ac97_read_cache(ac97, reg);
	new = (old & ~mask) | value;
	change = old != new;
	if (change) {
		ac97->regs[reg] = new;
		spin_unlock(&ac97->reg_lock);
		ac97->bus->write(ac97, reg, new);
	} else
		spin_unlock(&ac97->reg_lock);
	return change;
}

static int snd_ac97_ad18xx_update_pcm_bits(ac97_t *ac97, int codec, unsigned short mask, unsigned short value)
{
	int change;
	unsigned short old, new, cfg;

	down(&ac97->mutex);
	spin_lock(&ac97->reg_lock);
	old = ac97->spec.ad18xx.pcmreg[codec];
	new = (old & ~mask) | value;
	cfg = snd_ac97_read_cache(ac97, AC97_AD_SERIAL_CFG);
	change = old != new;
	if (change) {
		ac97->spec.ad18xx.pcmreg[codec] = new;
		spin_unlock(&ac97->reg_lock);
		/* select single codec */
		ac97->bus->write(ac97, AC97_AD_SERIAL_CFG,
				 (cfg & ~0x7000) |
				 ac97->spec.ad18xx.unchained[codec] | ac97->spec.ad18xx.chained[codec]);
		/* update PCM bits */
		ac97->bus->write(ac97, AC97_PCM, new);
		/* select all codecs */
		ac97->bus->write(ac97, AC97_AD_SERIAL_CFG,
				 cfg | 0x7000);
	} else
		spin_unlock(&ac97->reg_lock);
	up(&ac97->mutex);
	return change;
}

/*
 *
 */

static int snd_ac97_info_mux(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[8] = {
		"Mic", "CD", "Video", "Aux", "Line",
		"Mix", "Mix Mono", "Phone"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 2;
	uinfo->value.enumerated.items = 8;
	if (uinfo->value.enumerated.item > 7)
		uinfo->value.enumerated.item = 7;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_get_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	
	val = snd_ac97_read_cache(ac97, AC97_REC_SEL);
	ucontrol->value.enumerated.item[0] = (val >> 8) & 7;
	ucontrol->value.enumerated.item[1] = (val >> 0) & 7;
	return 0;
}

static int snd_ac97_put_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	
	if (ucontrol->value.enumerated.item[0] > 7 ||
	    ucontrol->value.enumerated.item[1] > 7)
		return -EINVAL;
	val = (ucontrol->value.enumerated.item[0] << 8) |
	      (ucontrol->value.enumerated.item[1] << 0);
	return snd_ac97_update(ac97, AC97_REC_SEL, val);
}

#define AC97_ENUM_DOUBLE(xname, reg, shift, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .info = snd_ac97_info_enum_double, \
  .get = snd_ac97_get_enum_double, .put = snd_ac97_put_enum_double, \
  .private_value = reg | (shift << 8) | (invert << 24) }

static int snd_ac97_info_enum_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts1[2] = { "pre 3D", "post 3D" };
	static char *texts2[2] = { "Mix", "Mic" };
	static char *texts3[2] = { "Mic1", "Mic2" };
	char **texts = NULL;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;

	switch (reg) {
	case AC97_GENERAL_PURPOSE:
		switch (shift) {
		case 15: texts = texts1; break;
		case 9: texts = texts2; break;
		case 8: texts = texts3; break;
		}
	}
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_get_enum_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	
	val = (snd_ac97_read_cache(ac97, reg) >> shift) & 1;
	if (invert)
		val ^= 1;
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int snd_ac97_put_enum_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	
	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;
	val = !!ucontrol->value.enumerated.item[0];
	if (invert)
		val = !val;
	return snd_ac97_update_bits(ac97, reg, 1 << shift, val << shift);
}

int snd_ac97_info_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

int snd_ac97_get_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0x01;
	
	ucontrol->value.integer.value[0] = (snd_ac97_read_cache(ac97, reg) >> shift) & mask;
	if (invert)
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
	return 0;
}

int snd_ac97_put_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0x01;
	unsigned short val;
	
	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = mask - val;
	return snd_ac97_update_bits(ac97, reg, mask << shift, val << shift);
}

#define AC97_DOUBLE(xname, reg, shift_left, shift_right, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), .info = snd_ac97_info_double, \
  .get = snd_ac97_get_double, .put = snd_ac97_put_double, \
  .private_value = (reg) | ((shift_left) << 8) | ((shift_right) << 12) | ((mask) << 16) | ((invert) << 24) }

static int snd_ac97_info_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_ac97_get_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift_left = (kcontrol->private_value >> 8) & 0x0f;
	int shift_right = (kcontrol->private_value >> 12) & 0x0f;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	
	spin_lock(&ac97->reg_lock);
	ucontrol->value.integer.value[0] = (snd_ac97_read_cache(ac97, reg) >> shift_left) & mask;
	ucontrol->value.integer.value[1] = (snd_ac97_read_cache(ac97, reg) >> shift_right) & mask;
	spin_unlock(&ac97->reg_lock);
	if (invert) {
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] = mask - ucontrol->value.integer.value[1];
	}
	return 0;
}

static int snd_ac97_put_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift_left = (kcontrol->private_value >> 8) & 0x0f;
	int shift_right = (kcontrol->private_value >> 12) & 0x0f;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	unsigned short val1, val2;
	
	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		val1 = mask - val1;
		val2 = mask - val2;
	}
	return snd_ac97_update_bits(ac97, reg, 
				    (mask << shift_left) | (mask << shift_right),
				    (val1 << shift_left) | (val2 << shift_right));
}

int snd_ac97_getput_page(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol,
			 int (*func)(snd_kcontrol_t *, snd_ctl_elem_value_t *))
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int err;

	if ((ac97->ext_id & AC97_EI_REV_MASK) >= AC97_EI_REV_23 &&
	    (reg >= 0x60 && reg < 0x70)) {
		unsigned short page_save;
		unsigned short page = (kcontrol->private_value >> 25) & 0x0f;
		down(&ac97->mutex); /* lock paging */
		page_save = snd_ac97_read(ac97, AC97_INT_PAGING) & AC97_PAGE_MASK;
		snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, page);
		err = func(kcontrol, ucontrol);
		snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, page_save);
		up(&ac97->mutex); /* unlock paging */
	} else
		err = func(kcontrol, ucontrol);
	return err;
}

/* for rev2.3 paging */
int snd_ac97_page_get_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	return snd_ac97_getput_page(kcontrol, ucontrol, snd_ac97_get_single);
}

/* for rev2.3 paging */
int snd_ac97_page_put_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	return snd_ac97_getput_page(kcontrol, ucontrol, snd_ac97_put_single);
}

static const snd_kcontrol_new_t snd_ac97_controls_master_mono[2] = {
AC97_SINGLE("Master Mono Playback Switch", AC97_MASTER_MONO, 15, 1, 1),
AC97_SINGLE("Master Mono Playback Volume", AC97_MASTER_MONO, 0, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_tone[2] = {
AC97_SINGLE("Tone Control - Bass", AC97_MASTER_TONE, 8, 15, 1),
AC97_SINGLE("Tone Control - Treble", AC97_MASTER_TONE, 0, 15, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_pc_beep[2] = {
AC97_SINGLE("PC Speaker Playback Switch", AC97_PC_BEEP, 15, 1, 1),
AC97_SINGLE("PC Speaker Playback Volume", AC97_PC_BEEP, 1, 15, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_phone[2] = {
AC97_SINGLE("Phone Playback Switch", AC97_PHONE, 15, 1, 1),
AC97_SINGLE("Phone Playback Volume", AC97_PHONE, 0, 15, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_mic[3] = {
AC97_SINGLE("Mic Playback Switch", AC97_MIC, 15, 1, 1),
AC97_SINGLE("Mic Playback Volume", AC97_MIC, 0, 15, 1),
AC97_SINGLE("Mic Boost (+20dB)", AC97_MIC, 6, 1, 0)
};

static const snd_kcontrol_new_t snd_ac97_control_capture_src = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Source",
	.info = snd_ac97_info_mux,
	.get = snd_ac97_get_mux,
	.put = snd_ac97_put_mux,
};

static const snd_kcontrol_new_t snd_ac97_control_capture_vol =
AC97_DOUBLE("Capture Volume", AC97_REC_GAIN, 8, 0, 15, 0);

static const snd_kcontrol_new_t snd_ac97_controls_mic_capture[2] = {
AC97_SINGLE("Mic Capture Switch", AC97_REC_GAIN_MIC, 15, 1, 1),
AC97_SINGLE("Mic Capture Volume", AC97_REC_GAIN_MIC, 0, 15, 0)
};

typedef enum {
	AC97_GENERAL_PCM_OUT = 0,
	AC97_GENERAL_STEREO_ENHANCEMENT,
	AC97_GENERAL_3D,
	AC97_GENERAL_LOUDNESS,
	AC97_GENERAL_MONO,
	AC97_GENERAL_MIC,
	AC97_GENERAL_LOOPBACK
} ac97_general_index_t;

static const snd_kcontrol_new_t snd_ac97_controls_general[7] = {
AC97_ENUM_DOUBLE("PCM Out Path & Mute", AC97_GENERAL_PURPOSE, 15, 0),
AC97_SINGLE("Simulated Stereo Enhancement", AC97_GENERAL_PURPOSE, 14, 1, 0),
AC97_SINGLE("3D Control - Switch", AC97_GENERAL_PURPOSE, 13, 1, 0),
AC97_SINGLE("Loudness (bass boost)", AC97_GENERAL_PURPOSE, 12, 1, 0),
AC97_ENUM_DOUBLE("Mono Output Select", AC97_GENERAL_PURPOSE, 9, 0),
AC97_ENUM_DOUBLE("Mic Select", AC97_GENERAL_PURPOSE, 8, 0),
AC97_SINGLE("ADC/DAC Loopback", AC97_GENERAL_PURPOSE, 7, 1, 0)
};

const snd_kcontrol_new_t snd_ac97_controls_3d[2] = {
AC97_SINGLE("3D Control - Center", AC97_3D_CONTROL, 8, 15, 0),
AC97_SINGLE("3D Control - Depth", AC97_3D_CONTROL, 0, 15, 0)
};

static const snd_kcontrol_new_t snd_ac97_controls_center[2] = {
AC97_SINGLE("Center Playback Switch", AC97_CENTER_LFE_MASTER, 7, 1, 1),
AC97_SINGLE("Center Playback Volume", AC97_CENTER_LFE_MASTER, 0, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_lfe[2] = {
AC97_SINGLE("LFE Playback Switch", AC97_CENTER_LFE_MASTER, 15, 1, 1),
AC97_SINGLE("LFE Playback Volume", AC97_CENTER_LFE_MASTER, 8, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_surround[2] = {
AC97_DOUBLE("Surround Playback Switch", AC97_SURROUND_MASTER, 15, 7, 1, 1),
AC97_DOUBLE("Surround Playback Volume", AC97_SURROUND_MASTER, 8, 0, 31, 1),
};

static const snd_kcontrol_new_t snd_ac97_control_eapd =
AC97_SINGLE("External Amplifier", AC97_POWERDOWN, 15, 1, 1);

static int snd_ac97_spdif_mask_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}
                        
static int snd_ac97_spdif_cmask_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ucontrol->value.iec958.status[0] = IEC958_AES0_PROFESSIONAL |
					   IEC958_AES0_NONAUDIO |
					   IEC958_AES0_CON_EMPHASIS_5015 |
					   IEC958_AES0_CON_NOT_COPYRIGHT;
	ucontrol->value.iec958.status[1] = IEC958_AES1_CON_CATEGORY |
					   IEC958_AES1_CON_ORIGINAL;
	ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS;
	return 0;
}
                        
static int snd_ac97_spdif_pmask_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	/* FIXME: AC'97 spec doesn't say which bits are used for what */
	ucontrol->value.iec958.status[0] = IEC958_AES0_PROFESSIONAL |
					   IEC958_AES0_NONAUDIO |
					   IEC958_AES0_PRO_FS |
					   IEC958_AES0_PRO_EMPHASIS_5015;
	return 0;
}

static int snd_ac97_spdif_default_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);

	spin_lock(&ac97->reg_lock);
	ucontrol->value.iec958.status[0] = ac97->spdif_status & 0xff;
	ucontrol->value.iec958.status[1] = (ac97->spdif_status >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (ac97->spdif_status >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (ac97->spdif_status >> 24) & 0xff;
	spin_unlock(&ac97->reg_lock);
	return 0;
}
                        
static int snd_ac97_spdif_default_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned int new = 0;
	unsigned short val = 0;
	int change;

	spin_lock(&ac97->reg_lock);
	new = val = ucontrol->value.iec958.status[0] & (IEC958_AES0_PROFESSIONAL|IEC958_AES0_NONAUDIO);
	if (ucontrol->value.iec958.status[0] & IEC958_AES0_PROFESSIONAL) {
		new |= ucontrol->value.iec958.status[0] & (IEC958_AES0_PRO_FS|IEC958_AES0_PRO_EMPHASIS_5015);
		switch (new & IEC958_AES0_PRO_FS) {
		case IEC958_AES0_PRO_FS_44100: val |= 0<<12; break;
		case IEC958_AES0_PRO_FS_48000: val |= 2<<12; break;
		case IEC958_AES0_PRO_FS_32000: val |= 3<<12; break;
		default:		       val |= 1<<12; break;
		}
		if ((new & IEC958_AES0_PRO_EMPHASIS) == IEC958_AES0_PRO_EMPHASIS_5015)
			val |= 1<<3;
	} else {
		new |= ucontrol->value.iec958.status[0] & (IEC958_AES0_CON_EMPHASIS_5015|IEC958_AES0_CON_NOT_COPYRIGHT);
		new |= ((ucontrol->value.iec958.status[1] & (IEC958_AES1_CON_CATEGORY|IEC958_AES1_CON_ORIGINAL)) << 8);
		new |= ((ucontrol->value.iec958.status[3] & IEC958_AES3_CON_FS) << 24);
		if ((new & IEC958_AES0_CON_EMPHASIS) == IEC958_AES0_CON_EMPHASIS_5015)
			val |= 1<<3;
		if (!(new & IEC958_AES0_CON_NOT_COPYRIGHT))
			val |= 1<<2;
		val |= ((new >> 8) & 0xff) << 4;	// category + original
		switch ((new >> 24) & 0xff) {
		case IEC958_AES3_CON_FS_44100: val |= 0<<12; break;
		case IEC958_AES3_CON_FS_48000: val |= 2<<12; break;
		case IEC958_AES3_CON_FS_32000: val |= 3<<12; break;
		default:		       val |= 1<<12; break;
		}
	}

	change = ac97->spdif_status != new;
	ac97->spdif_status = new;
	spin_unlock(&ac97->reg_lock);

	if (ac97->flags & AC97_CS_SPDIF) {
		int x = (val >> 12) & 0x03;
		switch (x) {
		case 0: x = 1; break;  // 44.1
		case 2: x = 0; break;  // 48.0
		default: x = 0; break; // illegal.
		}
		change |= snd_ac97_update_bits(ac97, AC97_CSR_SPDIF, 0x3fff, ((val & 0xcfff) | (x << 12)));
	} else if (ac97->flags & AC97_CX_SPDIF) {
		int v;
		v = new & (IEC958_AES0_CON_EMPHASIS_5015|IEC958_AES0_CON_NOT_COPYRIGHT) ? 0 : AC97_CXR_COPYRGT;
		v |= new & IEC958_AES0_NONAUDIO ? AC97_CXR_SPDIF_AC3 : AC97_CXR_SPDIF_PCM;
		change |= snd_ac97_update_bits(ac97, AC97_CXR_AUDIO_MISC, 
					       AC97_CXR_SPDIF_MASK | AC97_CXR_COPYRGT,
					       v);
	} else {
		unsigned short extst = snd_ac97_read_cache(ac97, AC97_EXTENDED_STATUS);
		snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0); /* turn off */

		change |= snd_ac97_update_bits(ac97, AC97_SPDIF, 0x3fff, val);
		if (extst & AC97_EA_SPDIF) {
			snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, AC97_EA_SPDIF); /* turn on again */
                }
	}

	return change;
}

static int snd_ac97_put_spsa(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	// int invert = (kcontrol->private_value >> 24) & 0xff;
	unsigned short value, old, new;

	value = (ucontrol->value.integer.value[0] & mask);

	mask <<= shift;
	value <<= shift;
	spin_lock(&ac97->reg_lock);
	old = snd_ac97_read_cache(ac97, reg);
	new = (old & ~mask) | value;
	spin_unlock(&ac97->reg_lock);

	if (old != new) {
		int change;
		unsigned short extst = snd_ac97_read_cache(ac97, AC97_EXTENDED_STATUS);
		snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0); /* turn off */
		change = snd_ac97_update_bits(ac97, reg, mask, value);
		if (extst & AC97_EA_SPDIF)
			snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, AC97_EA_SPDIF); /* turn on again */
		return change;
	}
	return 0;
}

const snd_kcontrol_new_t snd_ac97_controls_spdif[5] = {
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
		.info = snd_ac97_spdif_mask_info,
		.get = snd_ac97_spdif_cmask_get,
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),
		.info = snd_ac97_spdif_mask_info,
		.get = snd_ac97_spdif_pmask_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
		.info = snd_ac97_spdif_mask_info,
		.get = snd_ac97_spdif_default_get,
		.put = snd_ac97_spdif_default_put,
	},

	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH),AC97_EXTENDED_STATUS, 2, 1, 0),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "AC97-SPSA",
		.info = snd_ac97_info_single,
		.get = snd_ac97_get_single,
		.put = snd_ac97_put_spsa,
		.private_value = AC97_SINGLE_VALUE(AC97_EXTENDED_STATUS, 4, 3, 0)
	},
};

#define AD18XX_PCM_BITS(xname, codec, lshift, rshift, mask) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .info = snd_ac97_ad18xx_pcm_info_bits, \
  .get = snd_ac97_ad18xx_pcm_get_bits, .put = snd_ac97_ad18xx_pcm_put_bits, \
  .private_value = (codec) | ((lshift) << 8) | ((rshift) << 12) | ((mask) << 16) }

static int snd_ac97_ad18xx_pcm_info_bits(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int mask = (kcontrol->private_value >> 16) & 0x0f;
	int lshift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	if (lshift != rshift && (ac97->flags & AC97_STEREO_MUTES))
		uinfo->count = 2;
	else
		uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_ac97_ad18xx_pcm_get_bits(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->private_value & 3;
	int lshift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	
	ucontrol->value.integer.value[0] = mask - ((ac97->spec.ad18xx.pcmreg[codec] >> lshift) & mask);
	if (lshift != rshift && (ac97->flags & AC97_STEREO_MUTES))
		ucontrol->value.integer.value[1] = mask - ((ac97->spec.ad18xx.pcmreg[codec] >> rshift) & mask);
	return 0;
}

static int snd_ac97_ad18xx_pcm_put_bits(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->private_value & 3;
	int lshift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	unsigned short val, valmask;
	
	val = (mask - (ucontrol->value.integer.value[0] & mask)) << lshift;
	valmask = mask << lshift;
	if (lshift != rshift && (ac97->flags & AC97_STEREO_MUTES)) {
		val |= (mask - (ucontrol->value.integer.value[1] & mask)) << rshift;
		valmask |= mask << rshift;
	}
	return snd_ac97_ad18xx_update_pcm_bits(ac97, codec, valmask, val);
}

#define AD18XX_PCM_VOLUME(xname, codec) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .info = snd_ac97_ad18xx_pcm_info_volume, \
  .get = snd_ac97_ad18xx_pcm_get_volume, .put = snd_ac97_ad18xx_pcm_put_volume, \
  .private_value = codec }

static int snd_ac97_ad18xx_pcm_info_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 31;
	return 0;
}

static int snd_ac97_ad18xx_pcm_get_volume(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->private_value & 3;
	
	spin_lock(&ac97->reg_lock);
	ucontrol->value.integer.value[0] = 31 - ((ac97->spec.ad18xx.pcmreg[codec] >> 0) & 31);
	ucontrol->value.integer.value[1] = 31 - ((ac97->spec.ad18xx.pcmreg[codec] >> 8) & 31);
	spin_unlock(&ac97->reg_lock);
	return 0;
}

static int snd_ac97_ad18xx_pcm_put_volume(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->private_value & 3;
	unsigned short val1, val2;
	
	val1 = 31 - (ucontrol->value.integer.value[0] & 31);
	val2 = 31 - (ucontrol->value.integer.value[1] & 31);
	return snd_ac97_ad18xx_update_pcm_bits(ac97, codec, 0x1f1f, (val1 << 8) | val2);
}

static const snd_kcontrol_new_t snd_ac97_controls_ad18xx_pcm[2] = {
AD18XX_PCM_BITS("PCM Playback Switch", 0, 15, 7, 1),
AD18XX_PCM_VOLUME("PCM Playback Volume", 0)
};

static const snd_kcontrol_new_t snd_ac97_controls_ad18xx_surround[2] = {
AD18XX_PCM_BITS("Surround Playback Switch", 1, 15, 7, 1),
AD18XX_PCM_VOLUME("Surround Playback Volume", 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_ad18xx_center[2] = {
AD18XX_PCM_BITS("Center Playback Switch", 2, 15, 15, 1),
AD18XX_PCM_BITS("Center Playback Volume", 2, 8, 8, 31)
};

static const snd_kcontrol_new_t snd_ac97_controls_ad18xx_lfe[2] = {
AD18XX_PCM_BITS("LFE Playback Switch", 2, 7, 7, 1),
AD18XX_PCM_BITS("LFE Playback Volume", 2, 0, 0, 31)
};

/*
 *
 */

static void snd_ac97_powerdown(ac97_t *ac97);

static int snd_ac97_bus_free(ac97_bus_t *bus)
{
	if (bus) {
		snd_ac97_bus_proc_done(bus);
		if (bus->pcms)
			kfree(bus->pcms);
		if (bus->private_free)
			bus->private_free(bus);
		snd_magic_kfree(bus);
	}
	return 0;
}

static int snd_ac97_bus_dev_free(snd_device_t *device)
{
	ac97_bus_t *bus = snd_magic_cast(ac97_bus_t, device->device_data, return -ENXIO);
	return snd_ac97_bus_free(bus);
}

static int snd_ac97_free(ac97_t *ac97)
{
	if (ac97) {
		snd_ac97_proc_done(ac97);
		if (ac97->bus)
			ac97->bus->codec[ac97->num] = NULL;
		if (ac97->private_free)
			ac97->private_free(ac97);
		snd_magic_kfree(ac97);
	}
	return 0;
}

static int snd_ac97_dev_free(snd_device_t *device)
{
	ac97_t *ac97 = snd_magic_cast(ac97_t, device->device_data, return -ENXIO);
	snd_ac97_powerdown(ac97); /* for avoiding click noises during shut down */
	return snd_ac97_free(ac97);
}

static int snd_ac97_try_volume_mix(ac97_t * ac97, int reg)
{
	unsigned short val, mask = 0x8000;

	if (! snd_ac97_valid_reg(ac97, reg))
		return 0;

	switch (reg) {
	case AC97_MASTER_TONE:
		return ac97->caps & 0x04 ? 1 : 0;
	case AC97_HEADPHONE:
		return ac97->caps & 0x10 ? 1 : 0;
	case AC97_REC_GAIN_MIC:
		return ac97->caps & 0x01 ? 1 : 0;
	case AC97_3D_CONTROL:
		if (ac97->caps & 0x7c00) {
			val = snd_ac97_read(ac97, reg);
			/* if nonzero - fixed and we can't set it */
			return val == 0;
		}
		return 0;
	case AC97_CENTER_LFE_MASTER:	/* center */
		if ((ac97->ext_id & AC97_EI_CDAC) == 0)
			return 0;
		break;
	case AC97_CENTER_LFE_MASTER+1:	/* lfe */
		if ((ac97->ext_id & AC97_EI_LDAC) == 0)
			return 0;
		reg = AC97_CENTER_LFE_MASTER;
		mask = 0x0080;
		break;
	case AC97_SURROUND_MASTER:
		if ((ac97->ext_id & AC97_EI_SDAC) == 0)
			return 0;
		break;
	}

	if (ac97->limited_regs && test_bit(reg, ac97->reg_accessed))
		return 1; /* allow without check */

	val = snd_ac97_read(ac97, reg);
	if (!(val & mask)) {
		/* nothing seems to be here - mute flag is not set */
		/* try another test */
		snd_ac97_write_cache(ac97, reg, val | mask);
		val = snd_ac97_read(ac97, reg);
		if (!(val & mask))
			return 0;	/* nothing here */
	}
	return 1;		/* success, useable */
}

int snd_ac97_try_bit(ac97_t * ac97, int reg, int bit)
{
	unsigned short mask, val, orig, res;

	mask = 1 << bit;
	orig = snd_ac97_read(ac97, reg);
	val = orig ^ mask;
	snd_ac97_write(ac97, reg, val);
	res = snd_ac97_read(ac97, reg);
	snd_ac97_write_cache(ac97, reg, orig);
	return res == val;
}

static void snd_ac97_change_volume_params1(ac97_t * ac97, int reg, unsigned char *max)
{
	unsigned short val, val1;

	*max = 63;
	val = 0x8000 | 0x0020;
	snd_ac97_write(ac97, reg, val);
	val1 = snd_ac97_read(ac97, reg);
	if (val != val1) {
		*max = 31;
	}
	/* reset volume to zero */
	snd_ac97_write_cache(ac97, reg, 0x8000);
}

/* check the volume resolution of center/lfe */
static void snd_ac97_change_volume_params2(ac97_t * ac97, int reg, int shift, unsigned char *max)
{
	unsigned short val, val1;

	*max = 63;
	val = 0x8080 | (0x20 << shift);
	snd_ac97_write(ac97, reg, val);
	val1 = snd_ac97_read(ac97, reg);
	if (val != val1) {
		*max = 31;
	}
	/* reset volume to zero */
	snd_ac97_write_cache(ac97, reg, 0x8080);
}

/* check whether the volume resolution is 4 or 5 bits */
static void snd_ac97_change_volume_params3(ac97_t * ac97, int reg, unsigned char *max)
{
	unsigned short val, val1;

	*max = 31;
	val = 0x8000 | 0x0010;
	snd_ac97_write(ac97, reg, val);
	val1 = snd_ac97_read(ac97, reg);
	if (val != val1) {
		*max = 15;
	}
	/* reset volume to zero */
	snd_ac97_write_cache(ac97, reg, 0x8000);
}

/* check whether the volume is mono or stereo */
static int snd_ac97_is_stereo_vol(ac97_t *ac97, int reg)
{
	unsigned short val, val1, val2;
	val = snd_ac97_read(ac97, reg);
	val1 = val | 0x8000 | (0x01 << 8);
	snd_ac97_write(ac97, reg, val1);
	val2 = snd_ac97_read(ac97, reg);
	snd_ac97_write(ac97, reg, val); /* restore */
	return val1 == val2;
}

static inline int printable(unsigned int x)
{
	x &= 0xff;
	if (x < ' ' || x >= 0x71) {
		if (x <= 0x89)
			return x - 0x71 + 'A';
		return '?';
	}
	return x;
}

snd_kcontrol_t *snd_ac97_cnew(const snd_kcontrol_new_t *_template, ac97_t * ac97)
{
	snd_kcontrol_new_t template;
	memcpy(&template, _template, sizeof(template));
	snd_runtime_check(!template.index, return NULL);
	template.index = ac97->num;
	return snd_ctl_new1(&template, ac97);
}

/*
 * create mute switch(es) for normal stereo controls
 */
static int snd_ac97_cmute_new(snd_card_t *card, char *name, int reg, ac97_t *ac97)
{
	snd_kcontrol_t *kctl;
	int stereo = 0;

	if (ac97->flags & AC97_STEREO_MUTES) {
		/* check whether both mute bits work */
		unsigned short val, val1;
		val = snd_ac97_read(ac97, reg);
		val1 = val | 0x8080;
		snd_ac97_write(ac97, reg, val1);
		if (val1 == snd_ac97_read(ac97, reg))
			stereo = 1;
		snd_ac97_write(ac97, reg, val);
	}
	if (stereo) {
		snd_kcontrol_new_t tmp = AC97_DOUBLE(name, reg, 15, 7, 1, 1);
		tmp.index = ac97->num;
		kctl = snd_ctl_new1(&tmp, ac97);
	} else {
		snd_kcontrol_new_t tmp = AC97_SINGLE(name, reg, 15, 1, 1);
		tmp.index = ac97->num;
		kctl = snd_ctl_new1(&tmp, ac97);
	}
	return snd_ctl_add(card, kctl);
}

/*
 * create volumes for normal stereo controls
 */
static int snd_ac97_cvol_new(snd_card_t *card, char *name, int reg, unsigned int max, ac97_t *ac97)
{
	int err;
	snd_kcontrol_new_t tmp = AC97_DOUBLE(name, reg, 8, 0, (unsigned int)max, 1);
	tmp.index = ac97->num;
	if ((err = snd_ctl_add(card, snd_ctl_new1(&tmp, ac97))) < 0)
		return err;
	snd_ac97_write_cache(ac97, reg,
			     ((ac97->flags & AC97_STEREO_MUTES) ? 0x8080 : 0x8000) |
			     (unsigned short)max | ((unsigned short)max << 8));
	return 0;
}

/*
 * create mute-switch and volumes for normal stereo controls
 */
static int snd_ac97_cmix_new(snd_card_t *card, const char *pfx, int reg, int check_res, ac97_t *ac97)
{
	int err;
	char name[44];
	unsigned char max;

	sprintf(name, "%s Switch", pfx);
	if ((err = snd_ac97_cmute_new(card, name, reg, ac97)) < 0)
		return err;
	sprintf(name, "%s Volume", pfx);
	if (check_res)
		snd_ac97_change_volume_params1(ac97, reg, &max);
	else
		max = 31; /* 5bit */
	if ((err = snd_ac97_cvol_new(card, name, reg, max, ac97)) < 0)
		return err;
	return 0;
}


static unsigned int snd_ac97_determine_spdif_rates(ac97_t *ac97);

static int snd_ac97_mixer_build(ac97_t * ac97)
{
	snd_card_t *card = ac97->bus->card;
	snd_kcontrol_t *kctl;
	int err;
	unsigned int idx;
	unsigned char max;

	/* build master controls */
	/* AD claims to remove this control from AD1887, although spec v2.2 does not allow this */
	if (snd_ac97_try_volume_mix(ac97, AC97_MASTER)) {
		if ((err = snd_ac97_cmix_new(card, "Master Playback", AC97_MASTER, 1, ac97)) < 0)
			return err;
	}

	ac97->regs[AC97_CENTER_LFE_MASTER] = 0x8080;

	/* build center controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_CENTER_LFE_MASTER)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_center[0], ac97))) < 0)
			return err;
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_center[1], ac97))) < 0)
			return err;
		snd_ac97_change_volume_params2(ac97, AC97_CENTER_LFE_MASTER, 0, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		snd_ac97_write_cache(ac97, AC97_CENTER_LFE_MASTER, ac97->regs[AC97_CENTER_LFE_MASTER] | max);
	}

	/* build LFE controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_CENTER_LFE_MASTER+1)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_lfe[0], ac97))) < 0)
			return err;
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_lfe[1], ac97))) < 0)
			return err;
		snd_ac97_change_volume_params2(ac97, AC97_CENTER_LFE_MASTER, 8, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		snd_ac97_write_cache(ac97, AC97_CENTER_LFE_MASTER, ac97->regs[AC97_CENTER_LFE_MASTER] | max << 8);
	}

	/* build surround controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_SURROUND_MASTER)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_surround[0], ac97))) < 0)
			return err;
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_surround[1], ac97))) < 0)
			return err;
		snd_ac97_change_volume_params2(ac97, AC97_SURROUND_MASTER, 0, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		snd_ac97_write_cache(ac97, AC97_SURROUND_MASTER, 0x8080 | max | (max << 8));
	}

	/* build headphone controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_HEADPHONE)) {
		if ((err = snd_ac97_cmix_new(card, "Headphone Playback", AC97_HEADPHONE, 1, ac97)) < 0)
			return err;
	}
	
	/* build master mono controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_MASTER_MONO)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_master_mono[0], ac97))) < 0)
			return err;
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_master_mono[1], ac97))) < 0)
			return err;
		snd_ac97_change_volume_params1(ac97, AC97_MASTER_MONO, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		snd_ac97_write_cache(ac97, AC97_MASTER_MONO, 0x8000 | max);
	}
	
	/* build master tone controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_MASTER_TONE)) {
		for (idx = 0; idx < 2; idx++) {
			if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_tone[idx], ac97))) < 0)
				return err;
			if (ac97->id == AC97_ID_YMF753) {
				kctl->private_value &= ~(0xff << 16);
				kctl->private_value |= 7 << 16;
			}
		}
		snd_ac97_write_cache(ac97, AC97_MASTER_TONE, 0x0f0f);
	}
	
	/* build PC Speaker controls */
	if ((ac97->flags & AC97_HAS_PC_BEEP) ||
	    snd_ac97_try_volume_mix(ac97, AC97_PC_BEEP)) {
		for (idx = 0; idx < 2; idx++)
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_pc_beep[idx], ac97))) < 0)
				return err;
		snd_ac97_write_cache(ac97, AC97_PC_BEEP,
				     snd_ac97_read(ac97, AC97_PC_BEEP) | 0x801e);
	}
	
	/* build Phone controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_PHONE)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_phone[0], ac97))) < 0)
			return err;
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_phone[1], ac97))) < 0)
			return err;
		snd_ac97_change_volume_params3(ac97, AC97_PHONE, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		snd_ac97_write_cache(ac97, AC97_PHONE, 0x8000 | max);
	}
	
	/* build MIC controls */
	snd_ac97_change_volume_params3(ac97, AC97_MIC, &max);
	if (snd_ac97_is_stereo_vol(ac97, AC97_MIC)) {
		/* build stereo mic */
		if ((err = snd_ac97_cmute_new(card, "Mic Playback Switch", AC97_MIC, ac97)) < 0)
			return err;
		if ((err = snd_ac97_cvol_new(card, "Mic Playback Volume", AC97_MIC, max, ac97)) < 0)
			return err;
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_mic[2], ac97))) < 0)
			return err;
	} else {
		/* build mono mic */
		for (idx = 0; idx < 3; idx++) {
			if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_mic[idx], ac97))) < 0)
				return err;
			if (idx == 1) {		// volume
				kctl->private_value &= ~(0xff << 16);
				kctl->private_value |= (int)max << 16;
			}
		}
		snd_ac97_write_cache(ac97, AC97_MIC, 0x8000 | max);
	}

	/* build Line controls */
	if ((err = snd_ac97_cmix_new(card, "Line Playback", AC97_LINE, 0, ac97)) < 0)
		return err;
	
	/* build CD controls */
	if ((err = snd_ac97_cmix_new(card, "CD Playback", AC97_CD, 0, ac97)) < 0)
		return err;
	
	/* build Video controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_VIDEO)) {
		if ((err = snd_ac97_cmix_new(card, "Video Playback", AC97_VIDEO, 0, ac97)) < 0)
			return err;
	}

	/* build Aux controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_AUX)) {
		if ((err = snd_ac97_cmix_new(card, "Aux Playback", AC97_AUX, 0, ac97)) < 0)
			return err;
	}

	/* build PCM controls */
	if (ac97->flags & AC97_AD_MULTI) {
		unsigned short init_val;
		if (ac97->flags & AC97_STEREO_MUTES)
			init_val = 0x9f9f;
		else
			init_val = 0x9f1f;
		for (idx = 0; idx < 2; idx++)
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_ad18xx_pcm[idx], ac97))) < 0)
				return err;
		ac97->spec.ad18xx.pcmreg[0] = init_val;
		if (ac97->scaps & AC97_SCAP_SURROUND_DAC) {
			for (idx = 0; idx < 2; idx++)
				if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_ad18xx_surround[idx], ac97))) < 0)
					return err;
			ac97->spec.ad18xx.pcmreg[1] = init_val;
		}
		if (ac97->scaps & AC97_SCAP_CENTER_LFE_DAC) {
			for (idx = 0; idx < 2; idx++)
				if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_ad18xx_center[idx], ac97))) < 0)
					return err;
			for (idx = 0; idx < 2; idx++)
				if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_ad18xx_lfe[idx], ac97))) < 0)
					return err;
			ac97->spec.ad18xx.pcmreg[2] = init_val;
		}
		snd_ac97_write_cache(ac97, AC97_PCM, init_val);
	} else {
		if ((err = snd_ac97_cmute_new(card, "PCM Playback Switch", AC97_PCM, ac97)) < 0)
			return err;
		/* FIXME: C-Media chips have no PCM volume!! */
		if (ac97->id == AC97_ID_CM9739)
			snd_ac97_write_cache(ac97, AC97_PCM, 0x9f1f);
		else {
			if ((err = snd_ac97_cvol_new(card, "PCM Playback Volume", AC97_PCM, 31, ac97)) < 0)
				return err;
		}
	}

	/* build Capture controls */
	if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_control_capture_src, ac97))) < 0)
		return err;
	if ((err = snd_ac97_cmute_new(card, "Capture Switch", AC97_REC_GAIN, ac97)) < 0)
		return err;
	if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_control_capture_vol, ac97))) < 0)
		return err;
	snd_ac97_write_cache(ac97, AC97_REC_SEL, 0x0000);
	snd_ac97_write_cache(ac97, AC97_REC_GAIN, 0x0000);

	/* build MIC Capture controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_REC_GAIN_MIC)) {
		for (idx = 0; idx < 2; idx++)
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_mic_capture[idx], ac97))) < 0)
				return err;
		snd_ac97_write_cache(ac97, AC97_REC_GAIN_MIC, 0x0000);
	}

	/* build PCM out path & mute control */
	if (snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 15)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_PCM_OUT], ac97))) < 0)
			return err;
	}

	/* build Simulated Stereo Enhancement control */
	if (ac97->caps & 0x0008) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_STEREO_ENHANCEMENT], ac97))) < 0)
			return err;
	}

	/* build 3D Stereo Enhancement control */
	if (snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 13)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_3D], ac97))) < 0)
			return err;
	}

	/* build Loudness control */
	if (ac97->caps & 0x0020) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_LOUDNESS], ac97))) < 0)
			return err;
	}

	/* build Mono output select control */
	if (snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 9)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_MONO], ac97))) < 0)
			return err;
	}

	/* build Mic select control */
	if (snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 8)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_MIC], ac97))) < 0)
			return err;
	}

	/* build ADC/DAC loopback control */
	if (enable_loopback && snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 7)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_LOOPBACK], ac97))) < 0)
			return err;
	}

	snd_ac97_write_cache(ac97, AC97_GENERAL_PURPOSE, 0x0000);

	/* build 3D controls */
	if (ac97->build_ops && ac97->build_ops->build_3d) {
		ac97->build_ops->build_3d(ac97);
	} else {
		if (snd_ac97_try_volume_mix(ac97, AC97_3D_CONTROL)) {
			unsigned short val;
			val = 0x0707;
			snd_ac97_write(ac97, AC97_3D_CONTROL, val);
			val = snd_ac97_read(ac97, AC97_3D_CONTROL);
			val = val == 0x0606;
			if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
				return err;
			if (val)
				kctl->private_value = AC97_3D_CONTROL | (9 << 8) | (7 << 16);
			if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[1], ac97))) < 0)
				return err;
			if (val)
				kctl->private_value = AC97_3D_CONTROL | (1 << 8) | (7 << 16);
			snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
		}
	}

	/* build S/PDIF controls */
	if (ac97->ext_id & AC97_EI_SPDIF) {
		if (ac97->build_ops && ac97->build_ops->build_spdif) {
			if ((err = ac97->build_ops->build_spdif(ac97)) < 0)
				return err;
		} else {
			for (idx = 0; idx < 5; idx++)
				if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_spdif[idx], ac97))) < 0)
					return err;
			if (ac97->build_ops && ac97->build_ops->build_post_spdif) {
				if ((err = ac97->build_ops->build_post_spdif(ac97)) < 0)
					return err;
			}
			/* set default PCM S/PDIF params */
			/* consumer,PCM audio,no copyright,no preemphasis,PCM coder,original,48000Hz */
			snd_ac97_write_cache(ac97, AC97_SPDIF, 0x2a20);
			ac97->rates[AC97_RATES_SPDIF] = snd_ac97_determine_spdif_rates(ac97);
		}
		ac97->spdif_status = SNDRV_PCM_DEFAULT_CON_SPDIF;
	}
	
	/* build chip specific controls */
	if (ac97->build_ops && ac97->build_ops->build_specific)
		if ((err = ac97->build_ops->build_specific(ac97)) < 0)
			return err;

	if (snd_ac97_try_bit(ac97, AC97_POWERDOWN, 15)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_control_eapd, ac97))) < 0)
			return err;
	}

	return 0;
}

static int snd_ac97_modem_build(snd_card_t * card, ac97_t * ac97)
{
	/* TODO */
	//printk("AC97_GPIO_CFG = %x\n",snd_ac97_read(ac97,AC97_GPIO_CFG));
	snd_ac97_write(ac97, AC97_GPIO_CFG, 0xffff & ~(AC97_GPIO_LINE1_OH));
	snd_ac97_write(ac97, AC97_GPIO_POLARITY, 0xffff & ~(AC97_GPIO_LINE1_OH));
	snd_ac97_write(ac97, AC97_GPIO_STICKY, 0xffff);
	snd_ac97_write(ac97, AC97_GPIO_WAKEUP, 0x0);
	snd_ac97_write(ac97, AC97_MISC_AFE, 0x0);
	return 0;
}

static int snd_ac97_test_rate(ac97_t *ac97, int reg, int shadow_reg, int rate)
{
	unsigned short val;
	unsigned int tmp;

	tmp = ((unsigned int)rate * ac97->bus->clock) / 48000;
	snd_ac97_write_cache(ac97, reg, tmp & 0xffff);
	if (shadow_reg)
		snd_ac97_write_cache(ac97, shadow_reg, tmp & 0xffff);
	val = snd_ac97_read(ac97, reg);
	return val == (tmp & 0xffff);
}

static void snd_ac97_determine_rates(ac97_t *ac97, int reg, int shadow_reg, unsigned int *r_result)
{
	unsigned int result = 0;

	/* test a non-standard rate */
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 11000))
		result |= SNDRV_PCM_RATE_CONTINUOUS;
	/* let's try to obtain standard rates */
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 8000))
		result |= SNDRV_PCM_RATE_8000;
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 11025))
		result |= SNDRV_PCM_RATE_11025;
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 16000))
		result |= SNDRV_PCM_RATE_16000;
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 22050))
		result |= SNDRV_PCM_RATE_22050;
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 32000))
		result |= SNDRV_PCM_RATE_32000;
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 44100))
		result |= SNDRV_PCM_RATE_44100;
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 48000))
		result |= SNDRV_PCM_RATE_48000;
	*r_result = result;
}

/* check AC97_SPDIF register to accept which sample rates */
static unsigned int snd_ac97_determine_spdif_rates(ac97_t *ac97)
{
	unsigned int result = 0;
	int i;
	static unsigned short ctl_bits[] = {
		AC97_SC_SPSR_44K, AC97_SC_SPSR_32K, AC97_SC_SPSR_48K
	};
	static unsigned int rate_bits[] = {
		SNDRV_PCM_RATE_44100, SNDRV_PCM_RATE_32000, SNDRV_PCM_RATE_48000
	};

	for (i = 0; i < (int)ARRAY_SIZE(ctl_bits); i++) {
		snd_ac97_update_bits(ac97, AC97_SPDIF, AC97_SC_SPSR_MASK, ctl_bits[i]);
		if ((snd_ac97_read(ac97, AC97_SPDIF) & AC97_SC_SPSR_MASK) == ctl_bits[i])
			result |= rate_bits[i];
	}
	return result;
}

void snd_ac97_get_name(ac97_t *ac97, unsigned int id, char *name, int modem)
{
	const ac97_codec_id_t *pid;

	sprintf(name, "0x%x %c%c%c", id,
		printable(id >> 24),
		printable(id >> 16),
		printable(id >> 8));
	for (pid = snd_ac97_codec_id_vendors; pid->id; pid++)
		if (pid->id == (id & pid->mask)) {
			strcpy(name, pid->name);
			if (ac97) {
				if (!modem && pid->patch)
					pid->patch(ac97);
				else if (modem && pid->mpatch)
					pid->mpatch(ac97);
			} 
			goto __vendor_ok;
		}
	return;

      __vendor_ok:
	for (pid = snd_ac97_codec_ids; pid->id; pid++)
		if (pid->id == (id & pid->mask)) {
			strcat(name, " ");
			strcat(name, pid->name);
			if (pid->mask != 0xffffffff)
				sprintf(name + strlen(name), " rev %d", id & ~pid->mask);
			if (ac97) {
				if (!modem && pid->patch)
					pid->patch(ac97);
				else if (modem && pid->mpatch)
					pid->mpatch(ac97);
			}
			return;
		}
	sprintf(name + strlen(name), " id %x", id & 0xff);
}


/* wait for a while until registers are accessible after RESET
 * return 0 if ok, negative not ready
 */
static int ac97_reset_wait(ac97_t *ac97, int timeout, int with_modem)
{
	unsigned long end_time;
	end_time = jiffies + timeout;
	do {
		unsigned short ext_mid;
		
		/* use preliminary reads to settle the communication */
		snd_ac97_read(ac97, AC97_RESET);
		snd_ac97_read(ac97, AC97_VENDOR_ID1);
		snd_ac97_read(ac97, AC97_VENDOR_ID2);
		/* modem? */
		if (with_modem) {
			ext_mid = snd_ac97_read(ac97, AC97_EXTENDED_MID);
			if (ext_mid != 0xffff && (ext_mid & 1) != 0)
				return 0;
		}
		/* because the PCM or MASTER volume registers can be modified,
		 * the REC_GAIN register is used for tests
		 */
		/* test if we can write to the record gain volume register */
		snd_ac97_write_cache(ac97, AC97_REC_GAIN, 0x8a05);
		if (snd_ac97_read(ac97, AC97_REC_GAIN) == 0x8a05)
			return 0;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while (time_after_eq(end_time, jiffies));
	return -ENODEV;
}

/**
 * snd_ac97_bus - create an AC97 bus component
 * @card: the card instance
 * @_bus: the template of AC97 bus, callbacks and
 *         the private data.
 * @rbus: the pointer to store the new AC97 bus instance.
 *
 * Creates an AC97 bus component.  An ac97_bus_t instance is newly
 * allocated and initialized from the template (_bus).
 *
 * The template must include the valid callbacks (at least read and
 * write), the bus number (num), and the private data (private_data).
 * The other callbacks, wait and reset, are not mandatory.
 * 
 * The clock is set to 48000.  If another clock is needed, set
 * bus->clock manually.
 *
 * The AC97 bus instance is registered as a low-level device, so you don't
 * have to release it manually.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_ac97_bus(snd_card_t * card, ac97_bus_t * _bus, ac97_bus_t ** rbus)
{
	int err;
	ac97_bus_t *bus;
	static snd_device_ops_t ops = {
		.dev_free =	snd_ac97_bus_dev_free,
	};

	snd_assert(card != NULL, return -EINVAL);
	snd_assert(_bus != NULL && rbus != NULL, return -EINVAL);
	bus = snd_magic_kmalloc(ac97_bus_t, 0, GFP_KERNEL);
	if (bus == NULL)
		return -ENOMEM;
	*bus = *_bus;
	bus->card = card;
	if (bus->clock == 0)
		bus->clock = 48000;
	spin_lock_init(&bus->bus_lock);
	snd_ac97_bus_proc_init(bus);
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, bus, &ops)) < 0) {
		snd_ac97_bus_free(bus);
		return err;
	}
	*rbus = bus;
	return 0;
}

/**
 * snd_ac97_mixer - create an Codec97 component
 * @bus: the AC97 bus which codec is attached to
 * @_ac97: the template of ac97, including index, callbacks and
 *         the private data.
 * @rac97: the pointer to store the new ac97 instance.
 *
 * Creates an Codec97 component.  An ac97_t instance is newly
 * allocated and initialized from the template (_ac97).  The codec
 * is then initialized by the standard procedure.
 *
 * The template must include the valid callbacks (at least read and
 * write), the codec number (num) and address (addr), and the private
 * data (private_data).  The other callbacks, wait and reset, are not
 * mandatory.
 * 
 * The ac97 instance is registered as a low-level device, so you don't
 * have to release it manually.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_ac97_mixer(ac97_bus_t * bus, ac97_t * _ac97, ac97_t ** rac97)
{
	int err;
	ac97_t *ac97;
	snd_card_t *card;
	char name[64];
	unsigned long end_time;
	unsigned int reg;
	static snd_device_ops_t ops = {
		.dev_free =	snd_ac97_dev_free,
	};

	snd_assert(rac97 != NULL, return -EINVAL);
	*rac97 = NULL;
	snd_assert(bus != NULL && _ac97 != NULL, return -EINVAL);
	snd_assert(_ac97->num < 4 && bus->codec[_ac97->num] == NULL, return -EINVAL);
	card = bus->card;
	ac97 = snd_magic_kmalloc(ac97_t, 0, GFP_KERNEL);
	if (ac97 == NULL)
		return -ENOMEM;
	*ac97 = *_ac97;
	ac97->bus = bus;
	bus->codec[ac97->num] = ac97;
	spin_lock_init(&ac97->reg_lock);
	init_MUTEX(&ac97->mutex);

	if (ac97->pci) {
		pci_read_config_word(ac97->pci, PCI_SUBSYSTEM_VENDOR_ID, &ac97->subsystem_vendor);
		pci_read_config_word(ac97->pci, PCI_SUBSYSTEM_ID, &ac97->subsystem_device);
	}
	if (bus->reset) {
		bus->reset(ac97);
		goto __access_ok;
	}

	snd_ac97_write(ac97, AC97_RESET, 0);	/* reset to defaults */
	if (bus->wait)
		bus->wait(ac97);
	else {
		udelay(50);
		if (ac97->scaps & AC97_SCAP_SKIP_AUDIO)
			err = ac97_reset_wait(ac97, HZ/2, 1);
		else {
			err = ac97_reset_wait(ac97, HZ/2, 0);
			if (err < 0)
				err = ac97_reset_wait(ac97, 0, 1);
		}
		if (err < 0) {
			snd_printk(KERN_WARNING "AC'97 %d does not respond - RESET\n", ac97->num);
			/* proceed anyway - it's often non-critical */
		}
	}
      __access_ok:
	ac97->id = snd_ac97_read(ac97, AC97_VENDOR_ID1) << 16;
	ac97->id |= snd_ac97_read(ac97, AC97_VENDOR_ID2);
	if (ac97->id == 0x00000000 || ac97->id == 0xffffffff) {
		snd_printk(KERN_ERR "AC'97 %d access is not valid [0x%x], removing mixer.\n", ac97->num, ac97->id);
		snd_ac97_free(ac97);
		return -EIO;
	}
	
	/* test for AC'97 */
	if (!(ac97->scaps & AC97_SCAP_SKIP_AUDIO) && !(ac97->scaps & AC97_SCAP_AUDIO)) {
		/* test if we can write to the record gain volume register */
		snd_ac97_write_cache(ac97, AC97_REC_GAIN, 0x8a06);
		if ((err = snd_ac97_read(ac97, AC97_REC_GAIN)) == 0x8a06)
			ac97->scaps |= AC97_SCAP_AUDIO;
	}
	if (ac97->scaps & AC97_SCAP_AUDIO) {
		ac97->caps = snd_ac97_read(ac97, AC97_RESET);
		ac97->ext_id = snd_ac97_read(ac97, AC97_EXTENDED_ID);
		if (ac97->ext_id == 0xffff)	/* invalid combination */
			ac97->ext_id = 0;
	}

	/* test for MC'97 */
	if (!(ac97->scaps & AC97_SCAP_SKIP_MODEM) && !(ac97->scaps & AC97_SCAP_MODEM)) {
		ac97->ext_mid = snd_ac97_read(ac97, AC97_EXTENDED_MID);
		if (ac97->ext_mid == 0xffff)	/* invalid combination */
			ac97->ext_mid = 0;
		if (ac97->ext_mid & 1)
			ac97->scaps |= AC97_SCAP_MODEM;
	}

	if (!ac97_is_audio(ac97) && !ac97_is_modem(ac97)) {
		if (!(ac97->scaps & (AC97_SCAP_SKIP_AUDIO|AC97_SCAP_SKIP_MODEM)))
			snd_printk(KERN_ERR "AC'97 %d access error (not audio or modem codec)\n", ac97->num);
		snd_ac97_free(ac97);
		return -EACCES;
	}

	if (bus->reset) // FIXME: always skipping?
		goto __ready_ok;

	/* FIXME: add powerdown control */
	if (ac97_is_audio(ac97)) {
		/* nothing should be in powerdown mode */
		snd_ac97_write_cache(ac97, AC97_POWERDOWN, 0);
		snd_ac97_write_cache(ac97, AC97_RESET, 0);		/* reset to defaults */
		udelay(100);
		/* nothing should be in powerdown mode */
		snd_ac97_write_cache(ac97, AC97_POWERDOWN, 0);
		snd_ac97_write_cache(ac97, AC97_GENERAL_PURPOSE, 0);
		end_time = jiffies + (HZ / 10);
		do {
			if ((snd_ac97_read(ac97, AC97_POWERDOWN) & 0x0f) == 0x0f)
				goto __ready_ok;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		} while (time_after_eq(end_time, jiffies));
		snd_printk(KERN_WARNING "AC'97 %d analog subsections not ready\n", ac97->num);
	}

	/* FIXME: add powerdown control */
	if (ac97_is_modem(ac97)) {
		unsigned char tmp;

		/* nothing should be in powerdown mode */
		/* note: it's important to set the rate at first */
		tmp = AC97_MEA_GPIO;
		if (ac97->ext_mid & AC97_MEI_LINE1) {
			snd_ac97_write_cache(ac97, AC97_LINE1_RATE, 12000);
			tmp |= AC97_MEA_ADC1 | AC97_MEA_DAC1;
		}
		if (ac97->ext_mid & AC97_MEI_LINE2) {
			snd_ac97_write_cache(ac97, AC97_LINE2_RATE, 12000);
			tmp |= AC97_MEA_ADC2 | AC97_MEA_DAC2;
		}
		if (ac97->ext_mid & AC97_MEI_HANDSET) {
			snd_ac97_write_cache(ac97, AC97_HANDSET_RATE, 12000);
			tmp |= AC97_MEA_HADC | AC97_MEA_HDAC;
		}
		snd_ac97_write_cache(ac97, AC97_EXTENDED_MSTATUS, 0xff00 & ~(tmp << 8));
		udelay(100);
		/* nothing should be in powerdown mode */
		snd_ac97_write_cache(ac97, AC97_EXTENDED_MSTATUS, 0xff00 & ~(tmp << 8));
		end_time = jiffies + (HZ / 10);
		do {
			if ((snd_ac97_read(ac97, AC97_EXTENDED_MSTATUS) & tmp) == tmp)
				goto __ready_ok;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		} while (time_after_eq(end_time, jiffies));
		snd_printk(KERN_WARNING "MC'97 %d converters and GPIO not ready (0x%x)\n", ac97->num, snd_ac97_read(ac97, AC97_EXTENDED_MSTATUS));
	}
	
      __ready_ok:
	if (ac97_is_audio(ac97))
		ac97->addr = (ac97->ext_id & AC97_EI_ADDR_MASK) >> AC97_EI_ADDR_SHIFT;
	else
		ac97->addr = (ac97->ext_mid & AC97_MEI_ADDR_MASK) >> AC97_MEI_ADDR_SHIFT;
	if (ac97->ext_id & 0x0189)	/* L/R, MIC, SDAC, LDAC VRA support */
		snd_ac97_write_cache(ac97, AC97_EXTENDED_STATUS, ac97->ext_id & 0x0189);
	if (ac97->ext_id & AC97_EI_VRA) {	/* VRA support */
		snd_ac97_determine_rates(ac97, AC97_PCM_FRONT_DAC_RATE, 0, &ac97->rates[AC97_RATES_FRONT_DAC]);
		snd_ac97_determine_rates(ac97, AC97_PCM_LR_ADC_RATE, 0, &ac97->rates[AC97_RATES_ADC]);
	} else {
		ac97->rates[AC97_RATES_FRONT_DAC] = SNDRV_PCM_RATE_48000;
		ac97->rates[AC97_RATES_ADC] = SNDRV_PCM_RATE_48000;
	}
	if (ac97->ext_id & AC97_EI_SPDIF) {
		/* codec specific code (patch) should override these values */
		ac97->rates[AC97_RATES_SPDIF] = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_32000;
	}
	if (ac97->ext_id & AC97_EI_VRM) {	/* MIC VRA support */
		snd_ac97_determine_rates(ac97, AC97_PCM_MIC_ADC_RATE, 0, &ac97->rates[AC97_RATES_MIC_ADC]);
	} else {
		ac97->rates[AC97_RATES_MIC_ADC] = SNDRV_PCM_RATE_48000;
	}
	if (ac97->ext_id & AC97_EI_SDAC) {	/* SDAC support */
		snd_ac97_determine_rates(ac97, AC97_PCM_SURR_DAC_RATE, AC97_PCM_FRONT_DAC_RATE, &ac97->rates[AC97_RATES_SURR_DAC]);
		ac97->scaps |= AC97_SCAP_SURROUND_DAC;
	}
	if (ac97->ext_id & AC97_EI_LDAC) {	/* LDAC support */
		snd_ac97_determine_rates(ac97, AC97_PCM_LFE_DAC_RATE, AC97_PCM_FRONT_DAC_RATE, &ac97->rates[AC97_RATES_LFE_DAC]);
		ac97->scaps |= AC97_SCAP_CENTER_LFE_DAC;
	}
	/* additional initializations */
	if (bus->init)
		bus->init(ac97);
	snd_ac97_get_name(ac97, ac97->id, name, !ac97_is_audio(ac97));
	snd_ac97_get_name(NULL, ac97->id, name, !ac97_is_audio(ac97));  // ac97->id might be changed in the special setup code
	if (ac97_is_audio(ac97)) {
		if (card->mixername[0] == '\0') {
			strcpy(card->mixername, name);
		} else {
			if (strlen(card->mixername) + 1 + strlen(name) + 1 <= sizeof(card->mixername)) {
				strcat(card->mixername, ",");
				strcat(card->mixername, name);
			}
		}
		if ((err = snd_component_add(card, "AC97a")) < 0) {
			snd_ac97_free(ac97);
			return err;
		}
		if (snd_ac97_mixer_build(ac97) < 0) {
			snd_ac97_free(ac97);
			return -ENOMEM;
		}
	}
	if (ac97_is_modem(ac97)) {
		if (card->mixername[0] == '\0') {
			strcpy(card->mixername, name);
		} else {
			if (strlen(card->mixername) + 1 + strlen(name) + 1 <= sizeof(card->mixername)) {
				strcat(card->mixername, ",");
				strcat(card->mixername, name);
			}
		}
		if ((err = snd_component_add(card, "AC97m")) < 0) {
			snd_ac97_free(ac97);
			return err;
		}
		if (snd_ac97_modem_build(card, ac97) < 0) {
			snd_ac97_free(ac97);
			return -ENOMEM;
		}
	}
	/* make sure the proper powerdown bits are cleared */
	if (ac97->scaps) {
		reg = snd_ac97_read(ac97, AC97_EXTENDED_STATUS);
		if (ac97->scaps & AC97_SCAP_SURROUND_DAC) 
			reg &= ~AC97_EA_PRJ;
		if (ac97->scaps & AC97_SCAP_CENTER_LFE_DAC) 
			reg &= ~(AC97_EA_PRI | AC97_EA_PRK);
		snd_ac97_write_cache(ac97, AC97_EXTENDED_STATUS, reg);
	}
	snd_ac97_proc_init(ac97);
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, ac97, &ops)) < 0) {
		snd_ac97_free(ac97);
		return err;
	}
	*rac97 = ac97;
	return 0;
}


/*
 * Power down the chip.
 *
 * MASTER and HEADPHONE registers are muted but the register cache values
 * are not changed, so that the values can be restored in snd_ac97_resume().
 */
static void snd_ac97_powerdown(ac97_t *ac97)
{
	unsigned short power;

	if (ac97_is_audio(ac97)) {
		/* some codecs have stereo mute bits */
		snd_ac97_write(ac97, AC97_MASTER, 0x9f9f);
		snd_ac97_write(ac97, AC97_HEADPHONE, 0x9f9f);
	}

	power = ac97->regs[AC97_POWERDOWN] | 0x8000;	/* EAPD */
	power |= 0x4000;	/* Headphone amplifier powerdown */
	power |= 0x0300;	/* ADC & DAC powerdown */
	snd_ac97_write(ac97, AC97_POWERDOWN, power);
	udelay(100);
	power |= 0x0400;	/* Analog Mixer powerdown (Vref on) */
	snd_ac97_write(ac97, AC97_POWERDOWN, power);
	udelay(100);
#if 0
	/* FIXME: this causes click noises on some boards at resume */
	power |= 0x3800;	/* AC-link powerdown, internal Clk disable */
	snd_ac97_write(ac97, AC97_POWERDOWN, power);
#endif
}


#ifdef CONFIG_PM
/**
 * snd_ac97_suspend - General suspend function for AC97 codec
 * @ac97: the ac97 instance
 *
 * Suspends the codec, power down the chip.
 */
void snd_ac97_suspend(ac97_t *ac97)
{
	snd_ac97_powerdown(ac97);
}

/**
 * snd_ac97_resume - General resume function for AC97 codec
 * @ac97: the ac97 instance
 *
 * Do the standard resume procedure, power up and restoring the
 * old register values.
 */
void snd_ac97_resume(ac97_t *ac97)
{
	int i, is_ad18xx, codec;

	if (ac97->bus->reset) {
		ac97->bus->reset(ac97);
		goto  __reset_ready;
	}

	snd_ac97_write(ac97, AC97_POWERDOWN, 0);
	snd_ac97_write(ac97, AC97_RESET, 0);
	udelay(100);
	snd_ac97_write(ac97, AC97_POWERDOWN, 0);
	snd_ac97_write(ac97, AC97_GENERAL_PURPOSE, 0);

	snd_ac97_write(ac97, AC97_POWERDOWN, ac97->regs[AC97_POWERDOWN]);
	if (ac97_is_audio(ac97)) {
		ac97->bus->write(ac97, AC97_MASTER, 0x8101);
		for (i = HZ/10; i >= 0; i--) {
			if (snd_ac97_read(ac97, AC97_MASTER) == 0x8101)
				break;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		}
		/* FIXME: extra delay */
		ac97->bus->write(ac97, AC97_MASTER, 0x8000);
		if (snd_ac97_read(ac97, AC97_MASTER) != 0x8000) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ/4);
		}
	} else {
		for (i = HZ/10; i >= 0; i--) {
			unsigned short val = snd_ac97_read(ac97, AC97_EXTENDED_MID);
			if (val != 0xffff && (val & 1) != 0)
				break;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		}
	}
__reset_ready:

	if (ac97->bus->init)
		ac97->bus->init(ac97);

	is_ad18xx = (ac97->flags & AC97_AD_MULTI);
	if (is_ad18xx) {
		/* restore the AD18xx codec configurations */
		for (codec = 0; codec < 3; codec++) {
			if (! ac97->spec.ad18xx.id[codec])
				continue;
			/* select single codec */
			snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000,
					     ac97->spec.ad18xx.unchained[codec] | ac97->spec.ad18xx.chained[codec]);
			ac97->bus->write(ac97, AC97_AD_CODEC_CFG, ac97->spec.ad18xx.codec_cfg[codec]);
		}
		/* select all codecs */
		snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, 0x7000);
	}

	/* restore ac97 status */
	for (i = 2; i < 0x7c ; i += 2) {
		if (i == AC97_POWERDOWN || i == AC97_EXTENDED_ID)
			continue;
		/* restore only accessible registers
		 * some chip (e.g. nm256) may hang up when unsupported registers
		 * are accessed..!
		 */
		if (test_bit(i, ac97->reg_accessed)) {
			if (is_ad18xx) {
				/* handle multi codecs for AD18xx */
				if (i == AC97_PCM) {
					for (codec = 0; codec < 3; codec++) {
						if (! ac97->spec.ad18xx.id[codec])
							continue;
						/* select single codec */
						snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000,
								     ac97->spec.ad18xx.unchained[codec] | ac97->spec.ad18xx.chained[codec]);
						/* update PCM bits */
						ac97->bus->write(ac97, AC97_PCM, ac97->spec.ad18xx.pcmreg[codec]);
					}
					/* select all codecs */
					snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, 0x7000);
					continue;
				} else if (i == AC97_AD_TEST ||
					   i == AC97_AD_CODEC_CFG ||
					   i == AC97_AD_SERIAL_CFG)
					continue; /* ignore */
			}
			snd_ac97_write(ac97, i, ac97->regs[i]);
			snd_ac97_read(ac97, i);
		}
	}

	if (ac97->ext_id & AC97_EI_SPDIF) {
		if (ac97->regs[AC97_EXTENDED_STATUS] & AC97_EA_SPDIF) {
			/* reset spdif status */
			snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0);
			snd_ac97_write(ac97, AC97_EXTENDED_STATUS, ac97->regs[AC97_EXTENDED_STATUS]);
			if (ac97->flags & AC97_CS_SPDIF)
				snd_ac97_write(ac97, AC97_CSR_SPDIF, ac97->regs[AC97_CSR_SPDIF]);
			else
				snd_ac97_write(ac97, AC97_SPDIF, ac97->regs[AC97_SPDIF]);
			snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, AC97_EA_SPDIF); /* turn on again */
		}
	}
}
#endif


/*
 */
static void set_ctl_name(char *dst, const char *src, const char *suffix)
{
	if (suffix)
		sprintf(dst, "%s %s", src, suffix);
	else
		strcpy(dst, src);
}	

int snd_ac97_remove_ctl(ac97_t *ac97, const char *name, const char *suffix)
{
	snd_ctl_elem_id_t id;
	memset(&id, 0, sizeof(id));
	set_ctl_name(id.name, name, suffix);
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_remove_id(ac97->bus->card, &id);
}

static snd_kcontrol_t *ctl_find(ac97_t *ac97, const char *name, const char *suffix)
{
	snd_ctl_elem_id_t sid;
	memset(&sid, 0, sizeof(sid));
	set_ctl_name(sid.name, name, suffix);
	sid.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_find_id(ac97->bus->card, &sid);
}

int snd_ac97_rename_ctl(ac97_t *ac97, const char *src, const char *dst, const char *suffix)
{
	snd_kcontrol_t *kctl = ctl_find(ac97, src, suffix);
	if (kctl) {
		set_ctl_name(kctl->id.name, dst, suffix);
		return 0;
	}
	return -ENOENT;
}

/* rename both Volume and Switch controls - don't check the return value */
void snd_ac97_rename_vol_ctl(ac97_t *ac97, const char *src, const char *dst)
{
	snd_ac97_rename_ctl(ac97, src, dst, "Switch");
	snd_ac97_rename_ctl(ac97, src, dst, "Volume");
}

int snd_ac97_swap_ctl(ac97_t *ac97, const char *s1, const char *s2, const char *suffix)
{
	snd_kcontrol_t *kctl1, *kctl2;
	kctl1 = ctl_find(ac97, s1, suffix);
	kctl2 = ctl_find(ac97, s2, suffix);
	if (kctl1 && kctl2) {
		set_ctl_name(kctl1->id.name, s2, suffix);
		set_ctl_name(kctl2->id.name, s1, suffix);
		return 0;
	}
	return -ENOENT;
}

static int swap_headphone(ac97_t *ac97, int remove_master)
{
	if (remove_master) {
		if (ctl_find(ac97, "Headphone Playback Switch", NULL) == NULL)
			return 0;
		snd_ac97_remove_ctl(ac97, "Master Playback", "Switch");
		snd_ac97_remove_ctl(ac97, "Master Playback", "Volume");
	} else
		snd_ac97_rename_vol_ctl(ac97, "Master Playback", "Line-Out Playback");
	snd_ac97_rename_vol_ctl(ac97, "Headphone Playback", "Master Playback");
	return 0;
}

static int swap_surround(ac97_t *ac97)
{
	/* FIXME: error checks.. */
	snd_ac97_swap_ctl(ac97, "Master Playback", "Surround Playback", "Switch");
	snd_ac97_swap_ctl(ac97, "Master Playback", "Surround Playback", "Volume");
	return 0;
}

static int tune_ad_sharing(ac97_t *ac97)
{
	unsigned short scfg;
	if ((ac97->id & 0xffffff00) != 0x41445300) {
		snd_printk(KERN_ERR "ac97_quirk AD_SHARING is only for AD codecs\n");
		return -EINVAL;
	}
	/* Turn on OMS bit to route microphone to back panel */
	scfg = snd_ac97_read(ac97, AC97_AD_SERIAL_CFG);
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, scfg | 0x0200);
	return 0;
}

static const snd_kcontrol_new_t snd_ac97_alc_jack_detect = 
AC97_SINGLE("Jack Detect", AC97_ALC650_CLOCK, 5, 1, 0);

static int tune_alc_jack(ac97_t *ac97)
{
	if ((ac97->id & 0xffffff00) != 0x414c4700) {
		snd_printk(KERN_ERR "ac97_quirk ALC_JACK is only for Realtek codecs\n");
		return -EINVAL;
	}
	snd_ac97_update_bits(ac97, 0x7a, 0x20, 0x20); /* select jack detect function */
	snd_ac97_update_bits(ac97, 0x7a, 0x01, 0x01); /* Line-out auto mute */
	return snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&snd_ac97_alc_jack_detect, ac97));
}

static int apply_quirk(ac97_t *ac97, int quirk)
{
	switch (quirk) {
	case AC97_TUNE_NONE:
		return 0;
	case AC97_TUNE_HP_ONLY:
		return swap_headphone(ac97, 1);
	case AC97_TUNE_SWAP_HP:
		return swap_headphone(ac97, 0);
	case AC97_TUNE_SWAP_SURROUND:
		return swap_surround(ac97);
	case AC97_TUNE_AD_SHARING:
		return tune_ad_sharing(ac97);
	case AC97_TUNE_ALC_JACK:
		return tune_alc_jack(ac97);
	}
	return -EINVAL;
}

/**
 * snd_ac97_tune_hardware - tune up the hardware
 * @ac97: the ac97 instance
 * @quirk: quirk list
 * @override: explicit quirk value (overrides the list if not AC97_TUNE_DEFAULT)
 *
 * Do some workaround for each pci device, such as renaming of the
 * headphone (true line-out) control as "Master".
 * The quirk-list must be terminated with a zero-filled entry.
 *
 * Returns zero if successful, or a negative error code on failure.
 */

int snd_ac97_tune_hardware(ac97_t *ac97, struct ac97_quirk *quirk, int override)
{
	int result;

	snd_assert(quirk, return -EINVAL);

	if (override != AC97_TUNE_DEFAULT) {
		result = apply_quirk(ac97, override);
		if (result < 0)
			snd_printk(KERN_ERR "applying quirk type %d failed (%d)\n", override, result);
		return result;
	}

	for (; quirk->vendor; quirk++) {
		if (quirk->vendor != ac97->subsystem_vendor)
			continue;
		if ((! quirk->mask && quirk->device == ac97->subsystem_device) ||
		    quirk->device == (quirk->mask & ac97->subsystem_device)) {
			snd_printdd("ac97 quirk for %s (%04x:%04x)\n", quirk->name, ac97->subsystem_vendor, ac97->subsystem_device);
			result = apply_quirk(ac97, quirk->type);
			if (result < 0)
				snd_printk(KERN_ERR "applying quirk type %d for %s failed (%d)\n", quirk->type, quirk->name, result);
			return result;
		}
	}
	return 0;
}


/*
 *  Exported symbols
 */

EXPORT_SYMBOL(snd_ac97_write);
EXPORT_SYMBOL(snd_ac97_read);
EXPORT_SYMBOL(snd_ac97_write_cache);
EXPORT_SYMBOL(snd_ac97_update);
EXPORT_SYMBOL(snd_ac97_update_bits);
EXPORT_SYMBOL(snd_ac97_bus);
EXPORT_SYMBOL(snd_ac97_mixer);
EXPORT_SYMBOL(snd_ac97_pcm_assign);
EXPORT_SYMBOL(snd_ac97_pcm_open);
EXPORT_SYMBOL(snd_ac97_pcm_close);
EXPORT_SYMBOL(snd_ac97_tune_hardware);
EXPORT_SYMBOL(snd_ac97_set_rate);
#ifdef CONFIG_PM
EXPORT_SYMBOL(snd_ac97_resume);
EXPORT_SYMBOL(snd_ac97_suspend);
#endif

/*
 *  INIT part
 */

static int __init alsa_ac97_init(void)
{
	return 0;
}

static void __exit alsa_ac97_exit(void)
{
}

module_init(alsa_ac97_init)
module_exit(alsa_ac97_exit)
