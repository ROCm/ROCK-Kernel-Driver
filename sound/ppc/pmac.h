/*
 * Driver for PowerMac onboard soundchips
 * Copyright (c) 2001 by Takashi Iwai <tiwai@suse.de>
 *   based on dmasound.c.
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


#ifndef __PMAC_H
#define __PMAC_H

#include <sound/control.h>
#include <sound/pcm.h>
#include "awacs.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
#include <asm/adb.h>
#include <asm/cuda.h>
#include <asm/pmu.h>
#else /* 2.4.0 kernel */
#include <linux/adb.h>
#ifdef CONFIG_ADB_CUDA
#include <linux/cuda.h>
#endif
#ifdef CONFIG_ADB_PMU
#include <linux/pmu.h>
#endif
#endif
#include <linux/nvram.h>
#include <linux/vt_kern.h>
#include <asm/dbdma.h>
#include <asm/prom.h>
#include <asm/machdep.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0) || defined(CONFIG_ADB_CUDA)
#define PMAC_AMP_AVAIL
#endif

/* maximum number of fragments */
#define PMAC_MAX_FRAGS		32


/*
 * typedefs
 */
typedef struct snd_pmac pmac_t;
typedef struct snd_pmac_stream pmac_stream_t;
typedef struct snd_pmac_beep pmac_beep_t;
typedef struct snd_pmac_dbdma pmac_dbdma_t;


/*
 * DBDMA space
 */
struct snd_pmac_dbdma {
	unsigned long addr;
	struct dbdma_cmd *cmds;
	void *space;
	int size;
};

/*
 * playback/capture stream
 */
struct snd_pmac_stream {
	int running;	/* boolean */

	int stream;	/* PLAYBACK/CAPTURE */

	int dma_size; /* in bytes */
	int period_size; /* in bytes */
	int buffer_size; /* in kbytes */
	int nperiods, cur_period;

	pmac_dbdma_t cmd;
	volatile struct dbdma_regs *dma;

	snd_pcm_substream_t *substream;

	unsigned int cur_freqs;		/* currently available frequences */
	unsigned int cur_formats;	/* currently available formats */
};


/*
 * beep using pcm
 */
struct snd_pmac_beep {
	int running;	/* boolean */
	int volume;	/* mixer volume: 0-100 */
	int volume_play;	/* currently playing volume */
	int hz;
	int nsamples;
	short *buf;		/* allocated wave buffer */
	unsigned long addr;	/* physical address of buffer */
	struct timer_list timer;	/* timer list for stopping beep */
	void (*orig_mksound)(unsigned int, unsigned int);
				/* pointer to restore */
	snd_kcontrol_t *control;	/* mixer element */
};


/*
 */

enum snd_pmac_model {
	PMAC_AWACS, PMAC_SCREAMER, PMAC_BURGUNDY, PMAC_DACA, PMAC_TUMBLER
};

struct snd_pmac {
	snd_card_t *card;

	/* h/w info */
	struct device_node *node;
	unsigned int revision;
	unsigned int subframe;
	unsigned int device_id;
	enum snd_pmac_model model;

	unsigned int has_iic : 1;
	unsigned int is_pbook_3400 : 1;
	unsigned int is_pbook_G3 : 1;

	unsigned int can_byte_swap : 1;
	unsigned int can_duplex : 1;
	unsigned int can_capture : 1;

#ifdef PMAC_AMP_AVAIL
	unsigned int amp_only;
	int amp_vol[2];
#endif

	unsigned int initialized : 1;
	unsigned int feature_is_set : 1;

	int num_freqs;
	int *freq_table;
	unsigned int freqs_ok;		/* bit flags */
	unsigned int formats_ok;	/* pcm hwinfo */
	int active;
	int rate_index;
	int format;			/* current format */

	spinlock_t reg_lock;
	volatile struct awacs_regs *awacs;
	int awacs_reg[8]; /* register cache */

	unsigned char *latch_base;
	unsigned char *macio_base;

	pmac_stream_t playback;
	pmac_stream_t capture;

	pmac_dbdma_t extra_dma;

	int irq, tx_irq, rx_irq;

	snd_pcm_t *pcm;

	pmac_beep_t *beep;

	unsigned int control_mask;	/* control mask */

	/* mixer stuffs */
	void *mixer_data;
	void (*mixer_free)(pmac_t *);

	/* lowlevel callbacks */
	void (*set_format)(pmac_t *chip);
	void (*port_change)(pmac_t *chip);
#ifdef CONFIG_PMAC_PBOOK
	unsigned int sleep_registered : 1;
	void (*suspend)(pmac_t *chip);
	void (*resume)(pmac_t *chip);
#endif

};


/* exported functions */
int snd_pmac_new(snd_card_t *card, pmac_t **chip_return);
int snd_pmac_pcm_new(pmac_t *chip);
int snd_pmac_attach_beep(pmac_t *chip);

/* initialize mixer */
int snd_pmac_awacs_init(pmac_t *chip);
int snd_pmac_burgundy_init(pmac_t *chip);
int snd_pmac_daca_init(pmac_t *chip);
int snd_pmac_tumbler_init(pmac_t *chip);

/* i2c functions */
typedef struct snd_pmac_keywest {
	unsigned long base;
	int addr;
	int steps;
} pmac_keywest_t;

int snd_pmac_keywest_find(pmac_t *chip, pmac_keywest_t *i2c, int addr, int (*init_client)(pmac_t *, pmac_keywest_t *));
void snd_pmac_keywest_cleanup(pmac_keywest_t *i2c);
int snd_pmac_keywest_write(pmac_keywest_t *i2c, unsigned char cmd, int len, unsigned char *data);
inline static int snd_pmac_keywest_write_byte(pmac_keywest_t *i2c, unsigned char cmd, unsigned char data)
{
	return snd_pmac_keywest_write(i2c, cmd, 1, &data);
}


#endif /* __PMAC_H */
