/*
 * device driver for philips saa7134 based TV cards
 * tv audio decoder (fm stereo, nicam, ...)
 *
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>
#include <asm/div64.h>

#include "saa7134-reg.h"
#include "saa7134.h"

/* ------------------------------------------------------------------ */

static unsigned int audio_debug = 0;
MODULE_PARM(audio_debug,"i");
MODULE_PARM_DESC(audio_debug,"enable debug messages [tv audio]");

#define dprintk(fmt, arg...)	if (audio_debug) \
	printk(KERN_DEBUG "%s/audio: " fmt, dev->name, ## arg)

#define print_regb(reg) printk("%s:   reg 0x%03x [%-16s]: 0x%02x\n", \
		dev->name,(SAA7134_##reg),(#reg),saa_readb((SAA7134_##reg)))

/* ------------------------------------------------------------------ */

static struct saa7134_tvaudio tvaudio[] = {
	{
		.name          = "PAL-B/G FM-stereo",
		.std           = V4L2_STD_PAL,
		.mode          = TVAUDIO_FM_BG_STEREO,
		.carr1         = 5500,
		.carr2         = 5742,
	},{
		.name          = "PAL-D/K1 FM-stereo",
		.std           = V4L2_STD_PAL,
		.carr1         = 6500,
		.carr2         = 6258,
		.mode          = TVAUDIO_FM_BG_STEREO,
	},{
		.name          = "PAL-D/K2 FM-stereo",
		.std           = V4L2_STD_PAL,
		.carr1         = 6500,
		.carr2         = 6742,
		.mode          = TVAUDIO_FM_BG_STEREO,
	},{
		.name          = "PAL-D/K3 FM-stereo",
		.std           = V4L2_STD_PAL,
		.carr1         = 6500,
		.carr2         = 5742,
		.mode          = TVAUDIO_FM_BG_STEREO,
	},{
		.name          = "PAL-B/G NICAM",
		.std           = V4L2_STD_PAL,
		.carr1         = 5500,
		.carr2         = 5850,
		.mode          = TVAUDIO_NICAM_FM,
	},{
		.name          = "PAL-I NICAM",
		.std           = V4L2_STD_PAL,
		.carr1         = 6000,
		.carr2         = 6552,
		.mode          = TVAUDIO_NICAM_FM,
	},{
		.name          = "PAL-D/K NICAM",
		.std           = V4L2_STD_PAL,
		.carr1         = 6500,
		.carr2         = 5850,
		.mode          = TVAUDIO_NICAM_FM,
	},{
		.name          = "SECAM-L NICAM",
		.std           = V4L2_STD_SECAM,
		.carr1         = 6500,
		.carr2         = 5850,
		.mode          = TVAUDIO_NICAM_AM,
	},{
		.name          = "NTSC-M",
		.std           = V4L2_STD_NTSC,
		.carr1         = 4500,
		.carr2         = -1,
		.mode          = TVAUDIO_FM_MONO,
	},{
		.name          = "NTSC-A2 FM-stereo",
		.std           = V4L2_STD_NTSC,
		.carr1         = 4500,
		.carr2         = 4724,
		.mode          = TVAUDIO_FM_K_STEREO,
	}
};
#define TVAUDIO (sizeof(tvaudio)/sizeof(struct saa7134_tvaudio))

/* ------------------------------------------------------------------ */

static void tvaudio_init(struct saa7134_dev *dev)
{
	int clock = saa7134_boards[dev->board].audio_clock;

	/* init all audio registers */
	saa_writeb(SAA7134_AUDIO_PLL_CTRL,   0x00);
	if (need_resched())
		schedule();
	else
		udelay(10);
		
	saa_writeb(SAA7134_AUDIO_CLOCK0,      clock        & 0xff);
	saa_writeb(SAA7134_AUDIO_CLOCK1,     (clock >>  8) & 0xff);
	saa_writeb(SAA7134_AUDIO_CLOCK2,     (clock >> 16) & 0xff);
	saa_writeb(SAA7134_AUDIO_PLL_CTRL,   0x01);

	saa_writeb(SAA7134_NICAM_ERROR_LOW,  0x14);
	saa_writeb(SAA7134_NICAM_ERROR_HIGH, 0x50);
	saa_writeb(SAA7134_MONITOR_SELECT,   0xa0);
	saa_writeb(SAA7134_FM_DEMATRIX,      0x80);
}

static __u32 tvaudio_carr2reg(__u32 carrier)
{
	__u64 a = carrier;

	a <<= 24;
	do_div(a,12288);
	return a;
}

static void tvaudio_setcarrier(struct saa7134_dev *dev,
			       int primary, int secondary)
{
	if (-1 == secondary)
		secondary = primary;
	saa_writel(SAA7134_CARRIER1_FREQ0 >> 2, tvaudio_carr2reg(primary));
	saa_writel(SAA7134_CARRIER2_FREQ0 >> 2, tvaudio_carr2reg(secondary));
}

static void saa7134_tvaudio_do_mute_input(struct saa7134_dev *dev)
{
	int mute;
	struct saa7134_input *in;
	int reg = 0;
	int mask;

	/* look what is to do ... */
	in   = dev->input;
	mute = (dev->ctl_mute || dev->automute);
	if (!card_has_audio(dev) && card(dev).mute.name) {
		/* 7130 - we'll mute using some unconnected audio input */
		if (mute)
			in = &card(dev).mute;
	}
	if (dev->hw_mute  == mute &&
	    dev->hw_input == in)
		return;

#if 1
	dprintk("ctl_mute=%d automute=%d input=%s  =>  mute=%d input=%s\n",
		dev->ctl_mute,dev->automute,dev->input->name,mute,in->name);
#endif
	dev->hw_mute  = mute;
	dev->hw_input = in;

	if (card_has_audio(dev))
		/* 7134 mute */
		saa_writeb(SAA7134_AUDIO_MUTE_CTRL, mute ? 0xff : 0xbb);

	/* switch internal audio mux */
	switch (in->amux) {
	case TV:    reg = 0x02; break;
	case LINE1: reg = 0x00; break;
	case LINE2: reg = 0x01; break;
	}
	saa_andorb(SAA7134_ANALOG_IO_SELECT, 0x07, reg);

	/* switch gpio-connected external audio mux */
	if (0 == card(dev).gpiomask)
		return;
	mask = card(dev).gpiomask;
	saa_andorl(SAA7134_GPIO_GPMODE0 >> 2,   mask, mask);
	saa_andorl(SAA7134_GPIO_GPSTATUS0 >> 2, mask, in->gpio);
	saa7134_track_gpio(dev,in->name);
}

void saa7134_tvaudio_setmute(struct saa7134_dev *dev)
{
	saa7134_tvaudio_do_mute_input(dev);
}

void saa7134_tvaudio_setinput(struct saa7134_dev *dev,
			      struct saa7134_input *in)
{
	dev->input = in;
	saa7134_tvaudio_do_mute_input(dev);
}

void saa7134_tvaudio_setvolume(struct saa7134_dev *dev, int level)
{
	saa_writeb(SAA7134_CHANNEL1_LEVEL,     level & 0x1f);
	saa_writeb(SAA7134_CHANNEL2_LEVEL,     level & 0x1f);
	saa_writeb(SAA7134_NICAM_LEVEL_ADJUST, level & 0x1f);
}

static void tvaudio_setmode(struct saa7134_dev *dev,
			    struct saa7134_tvaudio *audio,
			    char *note)
{
	if (note)
		dprintk("tvaudio_setmode: %s %s [%d.%03d/%d.%03d MHz]\n",
			note,audio->name,
			audio->carr1 / 1000, audio->carr1 % 1000,
			audio->carr2 / 1000, audio->carr2 % 1000);

	if (dev->tvnorm->id == V4L2_STD_NTSC) {
		saa_writeb(SAA7134_AUDIO_CLOCKS_PER_FIELD0, 0xde);
		saa_writeb(SAA7134_AUDIO_CLOCKS_PER_FIELD1, 0x15);
		saa_writeb(SAA7134_AUDIO_CLOCKS_PER_FIELD2, 0x02);
	} else {
		saa_writeb(SAA7134_AUDIO_CLOCKS_PER_FIELD0, 0x00);
		saa_writeb(SAA7134_AUDIO_CLOCKS_PER_FIELD1, 0x80);
		saa_writeb(SAA7134_AUDIO_CLOCKS_PER_FIELD2, 0x02);
	}
	tvaudio_setcarrier(dev,audio->carr1,audio->carr2);
	
	switch (audio->mode) {
	case TVAUDIO_FM_MONO:
	case TVAUDIO_FM_BG_STEREO:
		saa_writeb(SAA7134_DEMODULATOR,               0x00);
		saa_writeb(SAA7134_DCXO_IDENT_CTRL,           0x00);
		saa_writeb(SAA7134_FM_DEEMPHASIS,             0x22);
		saa_writeb(SAA7134_FM_DEMATRIX,               0x80);
		saa_writeb(SAA7134_STEREO_DAC_OUTPUT_SELECT,  0xa0);
		break;
	case TVAUDIO_FM_K_STEREO:
		saa_writeb(SAA7134_DEMODULATOR,               0x00);
		saa_writeb(SAA7134_DCXO_IDENT_CTRL,           0x01);
		saa_writeb(SAA7134_FM_DEEMPHASIS,             0x22);
		saa_writeb(SAA7134_FM_DEMATRIX,               0x80);
		saa_writeb(SAA7134_STEREO_DAC_OUTPUT_SELECT,  0xa0);
		break;
	case TVAUDIO_NICAM_FM:
		saa_writeb(SAA7134_DEMODULATOR,               0x10);
		saa_writeb(SAA7134_DCXO_IDENT_CTRL,           0x00);
		saa_writeb(SAA7134_FM_DEEMPHASIS,             0x44);
		saa_writeb(SAA7134_STEREO_DAC_OUTPUT_SELECT,  0xa1);
		break;
	case TVAUDIO_NICAM_AM:
		saa_writeb(SAA7134_DEMODULATOR,               0x12);
		saa_writeb(SAA7134_DCXO_IDENT_CTRL,           0x00);
		saa_writeb(SAA7134_FM_DEEMPHASIS,             0x44);
		saa_writeb(SAA7134_STEREO_DAC_OUTPUT_SELECT,  0xa1);
		break;
	case TVAUDIO_FM_SAT_STEREO:
		/* not implemented (yet) */
		break;
	}
	saa_writel(0x174 >> 2, 0x0001e000); /* FIXME */
}

int saa7134_tvaudio_getstereo(struct saa7134_dev *dev,
			      struct saa7134_tvaudio *audio)
{
	__u32 idp,nicam;
	int retval = -1;
	
	switch (audio->mode) {
	case TVAUDIO_FM_MONO:
		return V4L2_TUNER_SUB_MONO;
	case TVAUDIO_FM_K_STEREO:
	case TVAUDIO_FM_BG_STEREO:
		idp = (saa_readb(SAA7134_IDENT_SIF) & 0xe0) >> 5;
		if (0x03 == (idp & 0x03))
			retval = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
		else if (0x05 == (idp & 0x05))
			retval = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
		else if (0x01 == (idp & 0x01))
			retval = V4L2_TUNER_SUB_MONO;
		break;
	case TVAUDIO_FM_SAT_STEREO:
		/* not implemented (yet) */
		break;
	case TVAUDIO_NICAM_FM:
	case TVAUDIO_NICAM_AM:
		nicam = saa_readb(SAA7134_NICAM_STATUS);
		switch (nicam & 0x0b) {
		case 0x08:
			retval = V4L2_TUNER_SUB_MONO;
			break;
		case 0x09:
			retval = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
			break;
		case 0x0a:
			retval = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
			break;
		}
		break;
	}
	if (retval != -1)
		dprintk("found audio subchannels:%s%s%s%s\n",
			(retval & V4L2_TUNER_SUB_MONO)   ? " mono"   : "",
			(retval & V4L2_TUNER_SUB_STEREO) ? " stereo" : "",
			(retval & V4L2_TUNER_SUB_LANG1)  ? " lang1"  : "",
			(retval & V4L2_TUNER_SUB_LANG2)  ? " lang2"  : "");
	return retval;
}

static int tvaudio_sleep(struct saa7134_dev *dev, int timeout)
{
	DECLARE_WAITQUEUE(wait, current);
	
	add_wait_queue(&dev->thread.wq, &wait);
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(timeout);
	remove_wait_queue(&dev->thread.wq, &wait);
	return dev->thread.scan1 != dev->thread.scan2;
}

static int tvaudio_checkcarrier(struct saa7134_dev *dev, int carrier)
{
	__s32 left,right,value;
	
	tvaudio_setcarrier(dev,carrier-100,carrier-100);
	if (tvaudio_sleep(dev,HZ/10))
		return -1;
	left = saa_readl(SAA7134_LEVEL_READOUT1 >> 2);
	if (tvaudio_sleep(dev,HZ/10))
		return -1;
	left = saa_readl(SAA7134_LEVEL_READOUT1 >> 2);

	tvaudio_setcarrier(dev,carrier+100,carrier+100);
	if (tvaudio_sleep(dev,HZ/10))
		return -1;
        right = saa_readl(SAA7134_LEVEL_READOUT1 >> 2);
	if (tvaudio_sleep(dev,HZ/10))
		return -1;
	right = saa_readl(SAA7134_LEVEL_READOUT1 >> 2);

	left >>= 16;
        right >>= 16;
	value = left > right ? left - right : right - left;
	dprintk("scanning %d.%03d MHz =>  dc is %5d [%d/%d]\n",
		carrier/1000,carrier%1000,value,left,right);
	return value;
}

#if 0
static void sifdebug_dump_regs(struct saa7134_dev *dev)
{
	print_regb(AUDIO_STATUS);
	print_regb(IDENT_SIF);
	print_regb(LEVEL_READOUT1);
	print_regb(LEVEL_READOUT2);
	print_regb(DCXO_IDENT_CTRL);
	print_regb(DEMODULATOR);
	print_regb(AGC_GAIN_SELECT);
	print_regb(MONITOR_SELECT);
	print_regb(FM_DEEMPHASIS);
	print_regb(FM_DEMATRIX);
	print_regb(SIF_SAMPLE_FREQ);
	print_regb(ANALOG_IO_SELECT);
}
#endif

static int tvaudio_thread(void *data)
{
#define MAX_SCAN 4
	static const int carr_pal[MAX_SCAN]     = { 5500, 6000, 6500 };
	static const int carr_ntsc[MAX_SCAN]    = { 4500 };
	static const int carr_secam[MAX_SCAN]   = { 6500 };
	static const int carr_default[MAX_SCAN] = { 4500, 5500, 6000, 6500 };
	struct saa7134_dev *dev = data;
	const int *carr_scan;
	int carr_vals[4];
	int i,max,carrier,audio;

	lock_kernel();
	daemonize("%s", dev->name);
	dev->thread.task = current;
	unlock_kernel();
	if (dev->thread.notify != NULL)
		up(dev->thread.notify);

	for (;;) {
		if (dev->thread.exit || signal_pending(current))
			goto done;
		interruptible_sleep_on(&dev->thread.wq);
		if (dev->thread.exit || signal_pending(current))
			goto done;

	restart:
		dev->thread.scan1 = dev->thread.scan2;
		dprintk("tvaudio thread scan start [%d]\n",dev->thread.scan1);
		dev->tvaudio  = NULL;
		tvaudio_init(dev);
		dev->automute = 1;
		saa7134_tvaudio_setmute(dev);

		/* give the tuner some time */
		if (tvaudio_sleep(dev,HZ/2))
			goto restart;

		/* find the main carrier */
		carr_scan = carr_default;
		if (dev->tvnorm->id & V4L2_STD_PAL)
			carr_scan = carr_pal;
		if (dev->tvnorm->id & V4L2_STD_NTSC)
			carr_scan = carr_ntsc;
		if (dev->tvnorm->id & V4L2_STD_SECAM)
			carr_scan = carr_secam;
		saa_writeb(SAA7134_MONITOR_SELECT,0x00);
		tvaudio_setmode(dev,&tvaudio[0],NULL);
		for (i = 0; i < MAX_SCAN; i++) {
			if (!carr_scan[i])
				continue;
			carr_vals[i] = tvaudio_checkcarrier(dev,carr_scan[i]);
			if (dev->thread.scan1 != dev->thread.scan2)
				goto restart;
		}
		for (carrier = 0, max = 0, i = 0; i < MAX_SCAN; i++) {
			if (!carr_scan[i])
				continue;
			if (max < carr_vals[i]) {
				max = carr_vals[i];
				carrier = carr_scan[i];
			}
		}
		if (0 == carrier) {
			/* Oops: autoscan didn't work for some reason :-/ */
			printk(KERN_WARNING "%s/audio: oops: audio carrier "
			       "scan failed\n", dev->name);
		} else {
			dprintk("found %s main sound carrier @ %d.%03d MHz\n",
				dev->tvnorm->name,
				carrier/1000,carrier%1000);
		}
		tvaudio_setcarrier(dev,carrier,carrier);
		dev->automute = 0;
		saa7134_tvaudio_setmute(dev);

		/* find the exact tv audio norm */
		for (audio = -1, i = 0; i < TVAUDIO; i++) {
			if (dev->tvnorm->id != -1 &&
			    dev->tvnorm->id != tvaudio[i].std)
				continue;
			if (tvaudio[i].carr1 != carrier)
				continue;

			if (-1 == audio)
				audio = i;
			tvaudio_setmode(dev,&tvaudio[i],"trying");
			if (tvaudio_sleep(dev,HZ))
				goto restart;
			if (-1 != saa7134_tvaudio_getstereo(dev,&tvaudio[i])) {
				audio = i;
				break;
			}
		}
		if (-1 == audio)
			continue;
		tvaudio_setmode(dev,&tvaudio[audio],"using");
		dev->tvaudio = &tvaudio[audio];

#if 1
		if (tvaudio_sleep(dev,3*HZ))
			goto restart;
		saa7134_tvaudio_getstereo(dev,&tvaudio[i]);
#endif
	}

 done:
	dev->thread.task = NULL;
	if(dev->thread.notify != NULL)
		up(dev->thread.notify);
	return 0;
}

/* ------------------------------------------------------------------ */

int saa7134_tvaudio_init(struct saa7134_dev *dev)
{
	DECLARE_MUTEX_LOCKED(sem);

	/* enable I2S audio output */
	if (saa7134_boards[dev->board].i2s_rate) {
		int rate = (32000 == saa7134_boards[dev->board].i2s_rate) ? 0x01 : 0x03;
		
		/* set rate */ 
		saa_andorb(SAA7134_SIF_SAMPLE_FREQ, 0x03, rate);

		/* enable I2S output */
		saa_writeb(SAA7134_DSP_OUTPUT_SELECT,  0x80);
		saa_writeb(SAA7134_I2S_OUTPUT_SELECT,  0x80);
		saa_writeb(SAA7134_I2S_OUTPUT_FORMAT,  0x01);
		saa_writeb(SAA7134_I2S_OUTPUT_LEVEL,   0x00);	
		saa_writeb(SAA7134_I2S_AUDIO_OUTPUT,   0x01);
	}

	/* start tvaudio thread */
	init_waitqueue_head(&dev->thread.wq);
	dev->thread.notify = &sem;
	kernel_thread(tvaudio_thread,dev,0);
	down(&sem);
	dev->thread.notify = NULL;
	wake_up_interruptible(&dev->thread.wq);

	return 0;
}

int saa7134_tvaudio_fini(struct saa7134_dev *dev)
{
	DECLARE_MUTEX_LOCKED(sem);

	/* shutdown tvaudio thread */
	if (dev->thread.task) {
		dev->thread.notify = &sem;
		dev->thread.exit = 1;
		wake_up_interruptible(&dev->thread.wq);
		down(&sem);
		dev->thread.notify = NULL;
	}
	saa_andorb(SAA7134_ANALOG_IO_SELECT, 0x07, 0x00); /* LINE1 */
	return 0;
}

int saa7134_tvaudio_do_scan(struct saa7134_dev *dev)
{
	dev->thread.scan2++;
	wake_up_interruptible(&dev->thread.wq);
	return 0;
}

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
