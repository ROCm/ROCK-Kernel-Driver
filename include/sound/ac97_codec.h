#ifndef __SOUND_AC97_CODEC_H
#define __SOUND_AC97_CODEC_H

/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.1
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

#include "control.h"
#include "info.h"

/*
 *  AC'97 codec registers
 */

#define AC97_RESET		0x00	/* Reset */
#define AC97_MASTER		0x02	/* Master Volume */
#define AC97_HEADPHONE		0x04	/* Headphone Volume (optional) */
#define AC97_MASTER_MONO	0x06	/* Master Volume Mono (optional) */
#define AC97_MASTER_TONE	0x08	/* Master Tone (Bass & Treble) (optional) */
#define AC97_PC_BEEP		0x0a	/* PC Beep Volume (optinal) */
#define AC97_PHONE		0x0c	/* Phone Volume (optional) */
#define AC97_MIC		0x0e	/* MIC Volume */
#define AC97_LINE		0x10	/* Line In Volume */
#define AC97_CD			0x12	/* CD Volume */
#define AC97_VIDEO		0x14	/* Video Volume (optional) */
#define AC97_AUX		0x16	/* AUX Volume (optional) */
#define AC97_PCM		0x18	/* PCM Volume */
#define AC97_REC_SEL		0x1a	/* Record Select */
#define AC97_REC_GAIN		0x1c	/* Record Gain */
#define AC97_REC_GAIN_MIC	0x1e	/* Record Gain MIC (optional) */
#define AC97_GENERAL_PURPOSE	0x20	/* General Purpose (optional) */
#define AC97_3D_CONTROL		0x22	/* 3D Control (optional) */
#define AC97_RESERVED		0x24	/* Reserved */
#define AC97_POWERDOWN		0x26	/* Powerdown control / status */
/* range 0x28-0x3a - AUDIO AC'97 2.0 extensions */
#define AC97_EXTENDED_ID	0x28	/* Extended Audio ID */
#define AC97_EXTENDED_STATUS	0x2a	/* Extended Audio Status */
#define AC97_PCM_FRONT_DAC_RATE 0x2c	/* PCM Front DAC Rate */
#define AC97_PCM_SURR_DAC_RATE	0x2e	/* PCM Surround DAC Rate */
#define AC97_PCM_LFE_DAC_RATE	0x30	/* PCM LFE DAC Rate */
#define AC97_PCM_LR_ADC_RATE	0x32	/* PCM LR DAC Rate */
#define AC97_PCM_MIC_ADC_RATE	0x34	/* PCM MIC ADC Rate */
#define AC97_CENTER_LFE_MASTER	0x36	/* Center + LFE Master Volume */
#define AC97_SURROUND_MASTER	0x38	/* Surround (Rear) Master Volume */
#define AC97_SPDIF		0x3a	/* S/PDIF control */
/* range 0x3c-0x58 - MODEM */
/* range 0x5a-0x7b - Vendor Specific */
#define AC97_VENDOR_ID1		0x7c	/* Vendor ID1 */
#define AC97_VENDOR_ID2		0x7e	/* Vendor ID2 / revision */

/* extended audio status and control bit defines */
#define AC97_EA_VRA		0x0001	/* Variable bit rate enable bit */
#define AC97_EA_DRA		0x0002	/* Double-rate audio enable bit */
#define AC97_EA_SPDIF		0x0004	/* S/PDIF Enable bit */
#define AC97_EA_VRM		0x0008	/* Variable bit rate for MIC enable bit */
#define AC97_EA_CDAC		0x0040	/* PCM Center DAC is ready (Read only) */
#define AC97_EA_SDAC		0x0040	/* PCM Surround DACs are ready (Read only) */
#define AC97_EA_LDAC		0x0080	/* PCM LFE DAC is ready (Read only) */
#define AC97_EA_MDAC		0x0100	/* MIC ADC is ready (Read only) */
#define AC97_EA_SPCV		0x0400	/* S/PDIF configuration valid (Read only) */
#define AC97_EA_PRI		0x0800	/* Turns the PCM Center DAC off */
#define AC97_EA_PRJ		0x1000	/* Turns the PCM Surround DACs off */
#define AC97_EA_PRK		0x2000	/* Turns the PCM LFE DAC off */
#define AC97_EA_PRL		0x4000	/* Turns the MIC ADC off */
#define AC97_EA_SLOT_MASK	0xffcf	/* Mask for slot assignment bits */
#define AC97_EA_SPSA_3_4	0x0000	/* Slot assigned to 3 & 4 */
#define AC97_EA_SPSA_7_8	0x0010	/* Slot assigned to 7 & 8 */
#define AC97_EA_SPSA_6_9	0x0020	/* Slot assigned to 6 & 9 */
#define AC97_EA_SPSA_10_11	0x0030	/* Slot assigned to 10 & 11 */

/* S/PDIF control bit defines */
#define AC97_SC_PRO		0x0001	/* Professional status */
#define AC97_SC_NAUDIO		0x0002	/* Non audio stream */
#define AC97_SC_COPY		0x0004	/* Copyright status */
#define AC97_SC_PRE		0x0008	/* Preemphasis status */
#define AC97_SC_CC_MASK		0x07f0	/* Category Code mask */
#define AC97_SC_L		0x0800	/* Generation Level status */
#define AC97_SC_SPSR_MASK	0xcfff	/* S/PDIF Sample Rate bits */
#define AC97_SC_SPSR_44K	0x0000	/* Use 44.1kHz Sample rate */
#define AC97_SC_SPSR_48K	0x2000	/* Use 48kHz Sample rate */
#define AC97_SC_SPSR_32K	0x3000	/* Use 32kHz Sample rate */
#define AC97_SC_DRS		0x4000	/* Double Rate S/PDIF */
#define AC97_SC_V		0x8000	/* Validity status */

/* specific - SigmaTel */
#define AC97_SIGMATEL_ANALOG	0x6c	/* Analog Special */
#define AC97_SIGMATEL_DAC2INVERT 0x6e
#define AC97_SIGMATEL_BIAS1	0x70
#define AC97_SIGMATEL_BIAS2	0x72
#define AC97_SIGMATEL_MULTICHN	0x74	/* Multi-Channel programming */
#define AC97_SIGMATEL_CIC1	0x76
#define AC97_SIGMATEL_CIC2	0x78

/* specific - Analog Devices */
#define AC97_AD_TEST		0x5a	/* test register */
#define AC97_AD_CODEC_CFG	0x70	/* codec configuration */
#define AC97_AD_JACK_SPDIF	0x72	/* Jack Sense & S/PDIF */
#define AC97_AD_SERIAL_CFG	0x74	/* Serial Configuration */
#define AC97_AD_MISC		0x76	/* Misc Control Bits */

/* specific - Cirrus Logic */
#define AC97_CSR_ACMODE		0x5e	/* AC Mode Register */
#define AC97_CSR_MISC_CRYSTAL	0x60	/* Misc Crystal Control */
#define AC97_CSR_SPDIF		0x68	/* S/PDIF Register */
#define AC97_CSR_SERIAL		0x6a	/* Serial Port Control */
#define AC97_CSR_SPECF_ADDR	0x6c	/* Special Feature Address */
#define AC97_CSR_SPECF_DATA	0x6e	/* Special Feature Data */
#define AC97_CSR_BDI_STATUS	0x7a	/* BDI Status */

/* specific - Conexant */
#define AC97_CXR_AUDIO_MISC	0x5c
#define AC97_CXR_SPDIFEN	(1<<3)
#define AC97_CXR_COPYRGT	(1<<2)
#define AC97_CXR_SPDIF_MASK	(3<<0)
#define AC97_CXR_SPDIF_PCM	0x0
#define AC97_CXR_SPDIF_AC3	0x2

/* ac97->scaps */
#define AC97_SCAP_SURROUND_DAC	(1<<0)	/* surround L&R DACs are present */
#define AC97_SCAP_CENTER_LFE_DAC (1<<1)	/* center and LFE DACs are present */

/* ac97->flags */
#define AC97_HAS_PC_BEEP	(1<<0)	/* force PC Speaker usage */
#define AC97_AD_MULTI		(1<<1)	/* Analog Devices - multi codecs */
#define AC97_CS_SPDIF		(1<<2)	/* Cirrus Logic uses funky SPDIF */
#define AC97_CX_SPDIF		(1<<3)	/* Conexant's spdif interface */

/*

 */

typedef struct _snd_ac97 ac97_t;

struct _snd_ac97 {
	void (*reset) (ac97_t *ac97);
	void (*write) (ac97_t *ac97, unsigned short reg, unsigned short val);
	unsigned short (*read) (ac97_t *ac97, unsigned short reg);
	void (*wait) (ac97_t *ac97);
	void (*init) (ac97_t *ac97);
	snd_info_entry_t *proc_entry;
	snd_info_entry_t *proc_regs_entry;
	void *private_data;
	void (*private_free) (ac97_t *ac97);
	/* --- */
	snd_card_t *card;
	spinlock_t reg_lock;
	unsigned short num;	/* number of codec: 0 = primary, 1 = secondary */
	unsigned short addr;	/* physical address of codec [0-3] */
	unsigned int id;	/* identification of codec */
	unsigned short caps;	/* capabilities (register 0) */
	unsigned short ext_id;	/* extended feature identification (register 28) */
	unsigned int scaps;	/* driver capabilities */
	unsigned int flags;	/* specific code */
	unsigned int clock;	/* AC'97 clock (usually 48000Hz) */
	unsigned int rates_front_dac;
	unsigned int rates_surr_dac;
	unsigned int rates_lfe_dac;
	unsigned int rates_adc;
	unsigned int rates_mic_adc;
	unsigned int spdif_status;
	unsigned short regs[0x80]; /* register cache */
	unsigned int limited_regs; /* allow limited registers only */
	bitmap_member(reg_accessed,0x80); /* bit flags */
	union {			/* vendor specific code */
		struct {
			unsigned short unchained[3];	// 0 = C34, 1 = C79, 2 = C69
			unsigned short chained[3];	// 0 = C34, 1 = C79, 2 = C69
			unsigned short id[3];		// codec IDs (lower 16-bit word)
			unsigned short pcmreg[3];	// PCM registers
			struct semaphore mutex;
		} ad18xx;
	} spec;
};

int snd_ac97_mixer(snd_card_t * card, ac97_t * _ac97, ac97_t ** rac97);

void snd_ac97_write(ac97_t *ac97, unsigned short reg, unsigned short value);
unsigned short snd_ac97_read(ac97_t *ac97, unsigned short reg);
void snd_ac97_write_cache(ac97_t *ac97, unsigned short reg, unsigned short value);
int snd_ac97_update(ac97_t *ac97, unsigned short reg, unsigned short value);
int snd_ac97_update_bits(ac97_t *ac97, unsigned short reg, unsigned short mask, unsigned short value);
int snd_ac97_set_rate(ac97_t *ac97, int reg, unsigned short rate);
#ifdef CONFIG_PM
void snd_ac97_suspend(ac97_t *ac97);
void snd_ac97_resume(ac97_t *ac97);
#endif

#endif /* __SOUND_AC97_CODEC_H */
