/*
 * linux/drivers/sound/waveartist.c
 *
 * The low level driver for the RWA010 Rockwell Wave Artist
 * codec chip used in the Rebel.com NetWinder.
 *
 * Cleaned up and integrated into 2.1 by Russell King (rmk@arm.linux.org.uk)
 * and Pat Beirne (patb@corel.ca)
 *
 *
 * Copyright (C) by Rebel.com 1998-1999
 *
 * RWA010 specs received under NDA from Rockwell
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Changes:
 * 11-10-2000	Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
 *		Added __init to waveartist_init()
 */

/* Debugging */
#define DEBUG_CMD	1
#define DEBUG_OUT	2
#define DEBUG_IN	4
#define DEBUG_INTR	8
#define DEBUG_MIXER	16
#define DEBUG_TRIGGER	32

#define debug_flg (0)

#include <linux/module.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/spinlock.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/system.h>

#include "sound_config.h"
#include "waveartist.h"

#ifndef _ISA_DMA
#define _ISA_DMA(x) (x)
#endif
#ifndef _ISA_IRQ
#define _ISA_IRQ(x) (x)
#endif

#define POSSIBLE_RECORDING_DEVICES	(SOUND_MASK_LINE       |\
					 SOUND_MASK_MIC	       |\
					 SOUND_MASK_LINE1)

#define SUPPORTED_MIXER_DEVICES		(SOUND_MASK_SYNTH      |\
					 SOUND_MASK_PCM        |\
					 SOUND_MASK_LINE       |\
					 SOUND_MASK_MIC        |\
					 SOUND_MASK_LINE1      |\
					 SOUND_MASK_RECLEV     |\
					 SOUND_MASK_VOLUME     |\
					 SOUND_MASK_IMIX)

static unsigned short levels[SOUND_MIXER_NRDEVICES] = {
	0x5555,		/* Master Volume	 */
	0x0000,		/* Bass			 */
	0x0000,		/* Treble		 */
	0x2323,		/* Synth (FM)		 */
	0x4b4b,		/* PCM			 */
	0x0000,		/* PC Speaker		 */
	0x0000,		/* Ext Line		 */
	0x0000,		/* Mic			 */
	0x0000,		/* CD			 */
	0x0000,		/* Recording monitor	 */
	0x0000,		/* SB PCM (ALT PCM)	 */
	0x0000,		/* Recording level	 */
	0x0000,		/* Input gain		 */
	0x0000,		/* Output gain		 */
	0x0000,		/* Line1 (Aux1)		 */
	0x0000,		/* Line2 (Aux2)		 */
	0x0000,		/* Line3 (Aux3)		 */
	0x0000,		/* Digital1		 */
	0x0000,		/* Digital2		 */
	0x0000,		/* Digital3		 */
	0x0000,		/* Phone In		 */
	0x0000,		/* Phone Out		 */
	0x0000,		/* Video		 */
	0x0000,		/* Radio		 */
	0x0000		/* Monitor		 */
};

typedef struct {
	struct address_info  hw;	/* hardware */
	char		*chip_name;

	int		xfer_count;
	int		audio_mode;
	int		open_mode;
	int		audio_flags;
	int		record_dev;
	int		playback_dev;
	int		dev_no;

	/* Mixer parameters */
	unsigned short	*levels;	   /* cache of volume settings   */
	int		recmask;	   /* currently enabled recording device! */
	int             supported_devices; /* SUPPORTED_MIXER_DEVICES    */
	int             rec_devices;	   /* POSSIBLE_RECORDING_DEVICES */

#ifdef CONFIG_ARCH_NETWINDER
	signed int	slider_vol;	   /* hardware slider volume     */
	unsigned int	handset_detect	:1;
	unsigned int	telephone_detect:1;
	unsigned int	no_autoselect	:1;/* handset/telephone autoselects a path */
	unsigned int	spkr_mute_state	:1;/* set by ioctl or autoselect */
	unsigned int	line_mute_state	:1;/* set by ioctl or autoselect */
	unsigned int	use_slider	:1;/* use slider setting for o/p vol */
#endif
} wavnc_info;

typedef struct wavnc_port_info {
	int		open_mode;
	int		speed;
	int		channels;
	int		audio_format;
} wavnc_port_info;

static int		nr_waveartist_devs;
static wavnc_info	adev_info[MAX_AUDIO_DEV];
static spinlock_t	waveartist_lock = SPIN_LOCK_UNLOCKED;

#ifndef machine_is_netwinder
#define machine_is_netwinder() 0
#endif

static struct timer_list vnc_timer;
static void vnc_configure_mixer(wavnc_info *devc);
static int vnc_private_ioctl(int dev, unsigned int cmd, caddr_t arg);
static void vnc_slider_tick(unsigned long data);

static inline void
waveartist_set_ctlr(struct address_info *hw, unsigned char clear, unsigned char set)
{
	unsigned int ctlr_port = hw->io_base + CTLR;

	clear = ~clear & inb(ctlr_port);

	outb(clear | set, ctlr_port);
}

/* Toggle IRQ acknowledge line
 */
static inline void
waveartist_iack(wavnc_info *devc)
{
	unsigned int ctlr_port = devc->hw.io_base + CTLR;
	int old_ctlr;

	old_ctlr = inb(ctlr_port) & ~IRQ_ACK;

	outb(old_ctlr | IRQ_ACK, ctlr_port);
	outb(old_ctlr, ctlr_port);
}

static inline int
waveartist_sleep(int timeout_ms)
{
	unsigned int timeout = timeout_ms * 10 * HZ / 100;

	do {
		current->state = TASK_INTERRUPTIBLE;
		timeout = schedule_timeout(timeout);
	} while (timeout);

	return 0;
}

static int
waveartist_reset(wavnc_info *devc)
{
	struct address_info *hw = &devc->hw;
	unsigned int timeout, res = -1;

	waveartist_set_ctlr(hw, -1, RESET);
	waveartist_sleep(2);
	waveartist_set_ctlr(hw, RESET, 0);

	timeout = 500;
	do {
		mdelay(2);

		if (inb(hw->io_base + STATR) & CMD_RF) {
			res = inw(hw->io_base + CMDR);
			if (res == 0x55aa)
				break;
		}
	} while (--timeout);

	if (timeout == 0) {
		printk(KERN_WARNING "WaveArtist: reset timeout ");
		if (res != (unsigned int)-1)
			printk("(res=%04X)", res);
		printk("\n");
		return 1;
	}
	return 0;
}

/* Helper function to send and receive words
 * from WaveArtist.  It handles all the handshaking
 * and can send or receive multiple words.
 */
static int
waveartist_cmd(wavnc_info *devc,
		int nr_cmd, unsigned int *cmd,
		int nr_resp, unsigned int *resp)
{
	unsigned int io_base = devc->hw.io_base;
	unsigned int timed_out = 0;
	unsigned int i;

	if (debug_flg & DEBUG_CMD) {
		printk("waveartist_cmd: cmd=");

		for (i = 0; i < nr_cmd; i++)
			printk("%04X ", cmd[i]);

		printk("\n");
	}

	if (inb(io_base + STATR) & CMD_RF) {
		int old_data;

		/* flush the port
		 */

		old_data = inw(io_base + CMDR);

		if (debug_flg & DEBUG_CMD)
			printk("flushed %04X...", old_data);

		udelay(10);
	}

	for (i = 0; !timed_out && i < nr_cmd; i++) {
		int count;

		for (count = 5000; count; count--)
			if (inb(io_base + STATR) & CMD_WE)
				break;

		if (!count)
			timed_out = 1;
		else
			outw(cmd[i], io_base + CMDR);
	}

	for (i = 0; !timed_out && i < nr_resp; i++) {
		int count;

		for (count = 5000; count; count--)
			if (inb(io_base + STATR) & CMD_RF)
				break;

		if (!count)
			timed_out = 1;
		else
			resp[i] = inw(io_base + CMDR);
	}

	if (debug_flg & DEBUG_CMD) {
		if (!timed_out) {
			printk("waveartist_cmd: resp=");

			for (i = 0; i < nr_resp; i++)
				printk("%04X ", resp[i]);

			printk("\n");
		} else
			printk("waveartist_cmd: timed out\n");
	}

	return timed_out ? 1 : 0;
}

/*
 * Send one command word
 */
static inline int
waveartist_cmd1(wavnc_info *devc, unsigned int cmd)
{
	return waveartist_cmd(devc, 1, &cmd, 0, NULL);
}

/*
 * Send one command, receive one word
 */
static inline unsigned int
waveartist_cmd1_r(wavnc_info *devc, unsigned int cmd)
{
	unsigned int ret;

	waveartist_cmd(devc, 1, &cmd, 1, &ret);

	return ret;
}

/*
 * Send a double command, receive one
 * word (and throw it away)
 */
static inline int
waveartist_cmd2(wavnc_info *devc, unsigned int cmd, unsigned int arg)
{
	unsigned int vals[2];

	vals[0] = cmd;
	vals[1] = arg;

	return waveartist_cmd(devc, 2, vals, 1, vals);
}

/*
 * Send a triple command
 */
static inline int
waveartist_cmd3(wavnc_info *devc, unsigned int cmd,
		unsigned int arg1, unsigned int arg2)
{
	unsigned int vals[3];

	vals[0] = cmd;
	vals[1] = arg1;
	vals[2] = arg2;

	return waveartist_cmd(devc, 3, vals, 0, NULL);
}

static int
waveartist_getrev(wavnc_info *devc, char *rev)
{
	unsigned int temp[2];
	unsigned int cmd = WACMD_GETREV;

	waveartist_cmd(devc, 1, &cmd, 2, temp);

	rev[0] = temp[0] >> 8;
	rev[1] = temp[0] & 255;
	rev[2] = '\0';

	return temp[0];
}

static void waveartist_halt_output(int dev);
static void waveartist_halt_input(int dev);
static void waveartist_halt(int dev);
static void waveartist_trigger(int dev, int state);

static int
waveartist_open(int dev, int mode)
{
	wavnc_info	*devc;
	wavnc_port_info	*portc;
	unsigned long	flags;

	if (dev < 0 || dev >= num_audiodevs)
		return -ENXIO;

	devc  = (wavnc_info *) audio_devs[dev]->devc;
	portc = (wavnc_port_info *) audio_devs[dev]->portc;

	spin_lock_irqsave(&waveartist_lock, flags);
	if (portc->open_mode || (devc->open_mode & mode)) {
		spin_unlock_irqrestore(&waveartist_lock, flags);
		return -EBUSY;
	}

	devc->audio_mode  = 0;
	devc->open_mode  |= mode;
	portc->open_mode  = mode;
	waveartist_trigger(dev, 0);

	if (mode & OPEN_READ)
		devc->record_dev = dev;
	if (mode & OPEN_WRITE)
		devc->playback_dev = dev;
	spin_unlock_irqrestore(&waveartist_lock, flags);

	return 0;
}

static void
waveartist_close(int dev)
{
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	unsigned long	flags;

	spin_lock_irqsave(&waveartist_lock, flags);

	waveartist_halt(dev);

	devc->audio_mode = 0;
	devc->open_mode &= ~portc->open_mode;
	portc->open_mode = 0;

	spin_unlock_irqrestore(&waveartist_lock, flags);
}

static void
waveartist_output_block(int dev, unsigned long buf, int __count, int intrflag)
{
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	unsigned long	flags;
	unsigned int	count = __count; 

	if (debug_flg & DEBUG_OUT)
		printk("waveartist: output block, buf=0x%lx, count=0x%x...\n",
			buf, count);
	/*
	 * 16 bit data
	 */
	if (portc->audio_format & (AFMT_S16_LE | AFMT_S16_BE))
		count >>= 1;

	if (portc->channels > 1)
		count >>= 1;

	count -= 1;

	if (devc->audio_mode & PCM_ENABLE_OUTPUT &&
	    audio_devs[dev]->flags & DMA_AUTOMODE &&
	    intrflag &&
	    count == devc->xfer_count) {
		devc->audio_mode |= PCM_ENABLE_OUTPUT;
		return;	/*
			 * Auto DMA mode on. No need to react
			 */
	}

	spin_lock_irqsave(&waveartist_lock, flags);

	/*
	 * set sample count
	 */
	waveartist_cmd2(devc, WACMD_OUTPUTSIZE, count);

	devc->xfer_count = count;
	devc->audio_mode |= PCM_ENABLE_OUTPUT;

	spin_unlock_irqrestore(&waveartist_lock, flags);
}

static void
waveartist_start_input(int dev, unsigned long buf, int __count, int intrflag)
{
	wavnc_port_info *portc = (wavnc_port_info *) audio_devs[dev]->portc;
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	unsigned long	flags;
	unsigned int	count = __count;

	if (debug_flg & DEBUG_IN)
		printk("waveartist: start input, buf=0x%lx, count=0x%x...\n",
			buf, count);

	if (portc->audio_format & (AFMT_S16_LE | AFMT_S16_BE))	/* 16 bit data */
		count >>= 1;

	if (portc->channels > 1)
		count >>= 1;

	count -= 1;

	if (devc->audio_mode & PCM_ENABLE_INPUT &&
	    audio_devs[dev]->flags & DMA_AUTOMODE &&
	    intrflag &&
	    count == devc->xfer_count) {
		devc->audio_mode |= PCM_ENABLE_INPUT;
		return;	/*
			 * Auto DMA mode on. No need to react
			 */
	}

	spin_lock_irqsave(&waveartist_lock, flags);

	/*
	 * set sample count
	 */
	waveartist_cmd2(devc, WACMD_INPUTSIZE, count);

	devc->xfer_count = count;
	devc->audio_mode |= PCM_ENABLE_INPUT;

	spin_unlock_irqrestore(&waveartist_lock, flags);
}

static int
waveartist_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	return -EINVAL;
}

static unsigned int
waveartist_get_speed(wavnc_port_info *portc)
{
	unsigned int speed;

	/*
	 * program the speed, channels, bits
	 */
	if (portc->speed == 8000)
		speed = 0x2E71;
	else if (portc->speed == 11025)
		speed = 0x4000;
	else if (portc->speed == 22050)
		speed = 0x8000;
	else if (portc->speed == 44100)
		speed = 0x0;
	else {
		/*
		 * non-standard - just calculate
		 */
		speed = portc->speed << 16;

		speed = (speed / 44100) & 65535;
	}

	return speed;
}

static unsigned int
waveartist_get_bits(wavnc_port_info *portc)
{
	unsigned int bits;

	if (portc->audio_format == AFMT_S16_LE)
		bits = 1;
	else if (portc->audio_format == AFMT_S8)
		bits = 0;
	else
		bits = 2;	//default AFMT_U8

	return bits;
}

static int
waveartist_prepare_for_input(int dev, int bsize, int bcount)
{
	unsigned long	flags;
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	unsigned int	speed, bits;

	if (devc->audio_mode)
		return 0;

	speed = waveartist_get_speed(portc);
	bits  = waveartist_get_bits(portc);

	spin_lock_irqsave(&waveartist_lock, flags);

	if (waveartist_cmd2(devc, WACMD_INPUTFORMAT, bits))
		printk(KERN_WARNING "waveartist: error setting the "
		       "record format to %d\n", portc->audio_format);

	if (waveartist_cmd2(devc, WACMD_INPUTCHANNELS, portc->channels))
		printk(KERN_WARNING "waveartist: error setting record "
		       "to %d channels\n", portc->channels);

	/*
	 * write cmd SetSampleSpeedTimeConstant
	 */
	if (waveartist_cmd2(devc, WACMD_INPUTSPEED, speed))
		printk(KERN_WARNING "waveartist: error setting the record "
		       "speed to %dHz.\n", portc->speed);

	if (waveartist_cmd2(devc, WACMD_INPUTDMA, 1))
		printk(KERN_WARNING "waveartist: error setting the record "
		       "data path to 0x%X\n", 1);

	if (waveartist_cmd2(devc, WACMD_INPUTFORMAT, bits))
		printk(KERN_WARNING "waveartist: error setting the record "
		       "format to %d\n", portc->audio_format);

	devc->xfer_count = 0;
	spin_unlock_irqrestore(&waveartist_lock, flags);
	waveartist_halt_input(dev);

	if (debug_flg & DEBUG_INTR) {
		printk("WA CTLR reg: 0x%02X.\n",
		       inb(devc->hw.io_base + CTLR));
		printk("WA STAT reg: 0x%02X.\n",
		       inb(devc->hw.io_base + STATR));
		printk("WA IRQS reg: 0x%02X.\n",
		       inb(devc->hw.io_base + IRQSTAT));
	}

	return 0;
}

static int
waveartist_prepare_for_output(int dev, int bsize, int bcount)
{
	unsigned long	flags;
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	unsigned int	speed, bits;

	/*
	 * program the speed, channels, bits
	 */
	speed = waveartist_get_speed(portc);
	bits  = waveartist_get_bits(portc);

	spin_lock_irqsave(&waveartist_lock, flags);

	if (waveartist_cmd2(devc, WACMD_OUTPUTSPEED, speed) &&
	    waveartist_cmd2(devc, WACMD_OUTPUTSPEED, speed))
		printk(KERN_WARNING "waveartist: error setting the playback "
		       "speed to %dHz.\n", portc->speed);

	if (waveartist_cmd2(devc, WACMD_OUTPUTCHANNELS, portc->channels))
		printk(KERN_WARNING "waveartist: error setting the playback "
		       "to %d channels\n", portc->channels);

	if (waveartist_cmd2(devc, WACMD_OUTPUTDMA, 0))
		printk(KERN_WARNING "waveartist: error setting the playback "
		       "data path to 0x%X\n", 0);

	if (waveartist_cmd2(devc, WACMD_OUTPUTFORMAT, bits))
		printk(KERN_WARNING "waveartist: error setting the playback "
		       "format to %d\n", portc->audio_format);

	devc->xfer_count = 0;
	spin_unlock_irqrestore(&waveartist_lock, flags);
	waveartist_halt_output(dev);

	if (debug_flg & DEBUG_INTR) {
		printk("WA CTLR reg: 0x%02X.\n",inb(devc->hw.io_base + CTLR));
		printk("WA STAT reg: 0x%02X.\n",inb(devc->hw.io_base + STATR));
		printk("WA IRQS reg: 0x%02X.\n",inb(devc->hw.io_base + IRQSTAT));
	}

	return 0;
}

static void
waveartist_halt(int dev)
{
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	wavnc_info	*devc;

	if (portc->open_mode & OPEN_WRITE)
		waveartist_halt_output(dev);

	if (portc->open_mode & OPEN_READ)
		waveartist_halt_input(dev);

	devc = (wavnc_info *) audio_devs[dev]->devc;
	devc->audio_mode = 0;
}

static void
waveartist_halt_input(int dev)
{
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	unsigned long	flags;

	spin_lock_irqsave(&waveartist_lock, flags);

	/*
	 * Stop capture
	 */
	waveartist_cmd1(devc, WACMD_INPUTSTOP);

	devc->audio_mode &= ~PCM_ENABLE_INPUT;

	/*
	 * Clear interrupt by toggling
	 * the IRQ_ACK bit in CTRL
	 */
	if (inb(devc->hw.io_base + STATR) & IRQ_REQ)
		waveartist_iack(devc);

//	devc->audio_mode &= ~PCM_ENABLE_INPUT;

	spin_unlock_irqrestore(&waveartist_lock, flags);
}

static void
waveartist_halt_output(int dev)
{
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	unsigned long	flags;

	spin_lock_irqsave(&waveartist_lock, flags);

	waveartist_cmd1(devc, WACMD_OUTPUTSTOP);

	devc->audio_mode &= ~PCM_ENABLE_OUTPUT;

	/*
	 * Clear interrupt by toggling
	 * the IRQ_ACK bit in CTRL
	 */
	if (inb(devc->hw.io_base + STATR) & IRQ_REQ)
		waveartist_iack(devc);

//	devc->audio_mode &= ~PCM_ENABLE_OUTPUT;

	spin_unlock_irqrestore(&waveartist_lock, flags);
}

static void
waveartist_trigger(int dev, int state)
{
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	unsigned long	flags;

	if (debug_flg & DEBUG_TRIGGER) {
		printk("wavnc: audio trigger ");
		if (state & PCM_ENABLE_INPUT)
			printk("in ");
		if (state & PCM_ENABLE_OUTPUT)
			printk("out");
		printk("\n");
	}

	spin_lock_irqsave(&waveartist_lock, flags);

	state &= devc->audio_mode;

	if (portc->open_mode & OPEN_READ &&
	    state & PCM_ENABLE_INPUT)
		/*
		 * enable ADC Data Transfer to PC
		 */
		waveartist_cmd1(devc, WACMD_INPUTSTART);

	if (portc->open_mode & OPEN_WRITE &&
	    state & PCM_ENABLE_OUTPUT)
		/*
		 * enable DAC data transfer from PC
		 */
		waveartist_cmd1(devc, WACMD_OUTPUTSTART);

	spin_unlock_irqrestore(&waveartist_lock, flags);
}

static int
waveartist_set_speed(int dev, int arg)
{
	wavnc_port_info *portc = (wavnc_port_info *) audio_devs[dev]->portc;

	if (arg <= 0)
		return portc->speed;

	if (arg < 5000)
		arg = 5000;
	if (arg > 44100)
		arg = 44100;

	portc->speed = arg;
	return portc->speed;

}

static short
waveartist_set_channels(int dev, short arg)
{
	wavnc_port_info *portc = (wavnc_port_info *) audio_devs[dev]->portc;

	if (arg != 1 && arg != 2)
		return portc->channels;

	portc->channels = arg;
	return arg;
}

static unsigned int
waveartist_set_bits(int dev, unsigned int arg)
{
	wavnc_port_info *portc = (wavnc_port_info *) audio_devs[dev]->portc;

	if (arg == 0)
		return portc->audio_format;

	if ((arg != AFMT_U8) && (arg != AFMT_S16_LE) && (arg != AFMT_S8))
		arg = AFMT_U8;

	portc->audio_format = arg;

	return arg;
}

static struct audio_driver waveartist_audio_driver = {
	owner:		THIS_MODULE,
	open:		waveartist_open,
	close:		waveartist_close,
	output_block:	waveartist_output_block,
	start_input:	waveartist_start_input,
	ioctl:		waveartist_ioctl,
	prepare_for_input:	waveartist_prepare_for_input,
	prepare_for_output:	waveartist_prepare_for_output,
	halt_io:	waveartist_halt,
	halt_input:	waveartist_halt_input,
	halt_output:	waveartist_halt_output,
	trigger:	waveartist_trigger,
	set_speed:	waveartist_set_speed,
	set_bits:	waveartist_set_bits,
	set_channels:	waveartist_set_channels
};


static void
waveartist_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	wavnc_info *devc = (wavnc_info *)dev_id;
	int	   irqstatus, status;

	irqstatus = inb(devc->hw.io_base + IRQSTAT);
	status    = inb(devc->hw.io_base + STATR);

	if (debug_flg & DEBUG_INTR)
		printk("waveartist_intr: stat=%02x, irqstat=%02x\n",
		       status, irqstatus);

	if (status & IRQ_REQ)	/* Clear interrupt */
		waveartist_iack(devc);
	else
		printk(KERN_WARNING "waveartist: unexpected interrupt\n");

	if (irqstatus & 0x01) {
		int temp = 1;

		/* PCM buffer done
		 */
		if ((status & DMA0) && (devc->audio_mode & PCM_ENABLE_OUTPUT)) {
			DMAbuf_outputintr(devc->playback_dev, 1);
			temp = 0;
		}
		if ((status & DMA1) && (devc->audio_mode & PCM_ENABLE_INPUT)) {
			DMAbuf_inputintr(devc->record_dev);
			temp = 0;
		}
		if (temp)	//default:
			printk(KERN_WARNING "waveartist: Unknown interrupt\n");
	}
	if (irqstatus & 0x2)
		// We do not use SB mode natively...
		printk(KERN_WARNING "waveartist: Unexpected SB interrupt...\n");
}

/* -------------------------------------------------------------------------
 * Mixer stuff
 */
static void
waveartist_mixer_update(wavnc_info *devc, int whichDev)
{
	unsigned int mask, reg_l, reg_r;
	unsigned int lev_left, lev_right;
	unsigned int vals[3];

	lev_left  = devc->levels[whichDev] & 0xff;
	lev_right = devc->levels[whichDev] >> 8;

#define SCALE(lev,max)	((lev) * (max) / 100)

	if (machine_is_netwinder() && whichDev == SOUND_MIXER_PHONEOUT)
		whichDev = SOUND_MIXER_VOLUME;

	switch(whichDev) {
	case SOUND_MIXER_VOLUME:
		mask  = 0x000e;
		reg_l = 0x200;
		reg_r = 0x600;
		lev_left  = SCALE(lev_left,  7) << 1;
		lev_right = SCALE(lev_right, 7) << 1;
		break;

	case SOUND_MIXER_LINE:
		if ((devc->recmask & SOUND_MASK_LINE) == 0)
			return;
		mask  = 0x07c0;
		reg_l = 0x000;
		reg_r = 0x400;
		lev_left  = SCALE(lev_left,  31) << 6;
		lev_right = SCALE(lev_right, 31) << 6;
		break;

	case SOUND_MIXER_MIC:
		if ((devc->recmask & SOUND_MASK_MIC) == 0)
			return;
		mask  = 0x0030;
		reg_l = 0x200;
		reg_r = 0x600;
		lev_left  = SCALE(lev_left,  3) << 4;
		lev_right = SCALE(lev_right, 3) << 4;
		break;

	case SOUND_MIXER_RECLEV:
		mask  = 0x000f;
		reg_l = 0x300;
		reg_r = 0x700;
		lev_left  = SCALE(lev_left,  10);
		lev_right = SCALE(lev_right, 10);
		break;

	case SOUND_MIXER_LINE1:
		if ((devc->recmask & SOUND_MASK_LINE1) == 0)
			return;
		mask  = 0x003e;
		reg_l = 0x000;
		reg_r = 0x400;
		lev_left  = SCALE(lev_left,  31) << 1;
		lev_right = SCALE(lev_right, 31) << 1;
		break;

	case SOUND_MIXER_PCM:
		waveartist_cmd3(devc, WACMD_SET_LEVEL,
				SCALE(lev_left,  32767),
				SCALE(lev_right, 32767));
		return;

	case SOUND_MIXER_SYNTH:
		waveartist_cmd3(devc, 0x0100 | WACMD_SET_LEVEL,
				SCALE(lev_left,  32767),
				SCALE(lev_right, 32767));
		return;

	default:
		return;
	}

	/* read left setting */
	vals[0] = reg_l + WACMD_GET_LEVEL;
	waveartist_cmd(devc, 1, vals, 1, vals + 1);

	/* read right setting */
	vals[0] = reg_r + 0x30;
	waveartist_cmd(devc, 1, vals, 1, vals + 2);

	vals[1] = (vals[1] & ~mask) | (lev_left  & mask);
	vals[2] = (vals[2] & ~mask) | (lev_right & mask);

	/* write left,right back */
	vals[0] = WACMD_SET_MIXER;
	waveartist_cmd(devc, 3, vals, 0, NULL);
}

static void
waveartist_select_input(wavnc_info *devc, unsigned int input)
{
	unsigned int vals[3];

	/*
	 * Get reg 9
	 */
	vals[0] = 0x0830;
	waveartist_cmd(devc, 1, vals, 1, vals + 1);

	/*
	 * Get reg 10, only so that we can write it back.
	 */
	vals[0] = 0x0930;
	waveartist_cmd(devc, 1, vals, 1, vals + 2);

	if (debug_flg & DEBUG_MIXER)
		printk("RECSRC: old left: 0x%04X, old right: 0x%04X.\n",
			vals[1] & 0x07, (vals[1] >> 3) & 0x07);

	/*
	 * kill current left/right mux input select
	 */
	vals[1] &= ~0x03F;

	switch (input) {
	case SOUND_MASK_MIC:
		/*
		 * right=mic, left=mic
		 */
		vals[1] |= 0x002D;
		break;

	case SOUND_MASK_LINE1:
		/*
		 * right=none, left=Aux1;
		 */
		vals[1] |= 0x0004;
		break;

	case SOUND_MASK_LINE:
		/*
		 * right=Line, left=Line;
		 */
		vals[1] |= 0x0012;
		break;
	}

	if (debug_flg & DEBUG_MIXER)
		printk("RECSRC %d: left=0x%04X, right=0x%04X.\n", input,
			vals[1] & 0x07, (vals[1] >> 3) & 0x07);

	/*
	 * and finally - write the reg pair back....
	 */
	vals[0] = WACMD_SET_MIXER;

	waveartist_cmd(devc, 3, vals, 0, NULL);
}

static int
waveartist_mixer_set(wavnc_info *devc, int whichDev, unsigned int level)
{
	unsigned int lev_left  = level & 0x007f;
	unsigned int lev_right = (level & 0x7f00) >> 8;
	int left, right, devmask;

	left = level & 0x7f;
	right = (level & 0x7f00) >> 8;

	if (debug_flg & DEBUG_MIXER)
		printk("wa_mixer_set(dev=%d, level=%X)\n",
			whichDev, level);

	switch (whichDev) {
	case SOUND_MIXER_VOLUME:	/* master volume (0-7)       */
	case SOUND_MIXER_LINE:		/* external line (0-31)      */
	case SOUND_MIXER_MIC:		/* mono mic (0-3)            */
	case SOUND_MIXER_RECLEV:	/* recording level (0-7)     */
	case SOUND_MIXER_LINE1:		/* mono external aux1 (0-31) */
	case SOUND_MIXER_PCM:		/* Waveartist PCM (0-32767)  */
	case SOUND_MIXER_SYNTH:		/* internal synth (0-31)     */
	case SOUND_MIXER_IMIX:		/* recording feedback        */
		devc->levels[whichDev] = lev_left | lev_right << 8;
		waveartist_mixer_update(devc, whichDev);
		break;

	/* Select recording input source
	 */
	case SOUND_MIXER_RECSRC:
		devmask = level & devc->rec_devices;

#ifdef CONFIG_ARCH_NETWINDER
		if (machine_is_netwinder())
			vnc_configure_mixer(devc);
		else
#endif
		{
			waveartist_select_input(devc, level);

			/*
			 * if record monitoring is on, make sure the bit is set
			 */
			if (devc->levels[SOUND_MIXER_IMIX])
				waveartist_mixer_update(devc, SOUND_MIXER_IMIX);
		}

		/*
		 * do not save in "levels", return current setting
		 */
		return devc->recmask;

	default:
		return -EINVAL;
	}

	return devc->levels[whichDev];
}

static void
waveartist_mixer_reset(wavnc_info *devc)
{
	int i;

	if (debug_flg & DEBUG_MIXER)
		printk("%s: mixer_reset\n", devc->hw.name);

	/*
	 * reset mixer cmd
	 */
	waveartist_cmd1(devc, WACMD_RST_MIXER);

	/*
	 * set input for ADC to come from 'quiet'
	 * turn on default modes
	 */
	waveartist_cmd3(devc, WACMD_SET_MIXER, 0x9800, 0xa836);

	/*
	 * set mixer input select to none, RX filter gains 0 db
	 */
	waveartist_cmd3(devc, WACMD_SET_MIXER, 0x4c00, 0x8c00);

	/*
	 * set bit 0 reg 2 to 1 - unmute MonoOut
	 */
	waveartist_cmd3(devc, WACMD_SET_MIXER, 0x2801, 0x6800);

	/* set default input device = internal mic
	 * current recording device = none
	 */
	devc->recmask = 0;

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		waveartist_mixer_update(devc, i);

	devc->supported_devices = SUPPORTED_MIXER_DEVICES;
	devc->rec_devices = POSSIBLE_RECORDING_DEVICES;

	if (machine_is_netwinder()) {
		devc->supported_devices |= SOUND_MASK_PHONEIN | SOUND_MASK_PHONEOUT;
		devc->rec_devices |= SOUND_MASK_PHONEIN;
	}
}

static int
waveartist_mixer_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	wavnc_info *devc = (wavnc_info *)audio_devs[dev]->devc;
	int ret;

#ifdef CONFIG_ARCH_NETWINDER
	if (machine_is_netwinder()) {
		ret = vnc_private_ioctl(dev, cmd, arg);
		if (ret != -ENOIOCTLCMD)
			return ret;
	}
#endif

	if (((cmd >> 8) & 0xff) == 'M') {
		if (_SIOC_DIR(cmd) & _SIOC_WRITE) {
			int val;

			if (get_user(val, (int *)arg))
				return -EFAULT;

			return waveartist_mixer_set(devc, cmd & 0xff, val);
		} else {
			/*
			 * Return parameters
			 */
			switch (cmd & 0xff) {
			case SOUND_MIXER_RECSRC:
				ret = devc->recmask;
				break;

			case SOUND_MIXER_DEVMASK:
				ret = devc->supported_devices;
				break;

			case SOUND_MIXER_STEREODEVS:
				ret = devc->supported_devices &
					~(SOUND_MASK_SPEAKER|SOUND_MASK_IMIX);
				break;

			case SOUND_MIXER_RECMASK:
				ret = devc->rec_devices;
				break;

			case SOUND_MIXER_CAPS:
				ret = SOUND_CAP_EXCL_INPUT;
				break;

			default:
				if ((cmd & 0xff) < SOUND_MIXER_NRDEVICES)
					ret = devc->levels[cmd & 0xff];
				else
					return -EINVAL;
			}

			return put_user(ret, (int *)arg) ? -EFAULT : 0;
		}
	}

	return -ENOIOCTLCMD;
}

static struct mixer_operations waveartist_mixer_operations =
{
	owner:	THIS_MODULE,
	id:	"WaveArtist",
	name:	"WaveArtist NetWinder",
	ioctl:	waveartist_mixer_ioctl
};

static int __init waveartist_init(wavnc_info *devc)
{
	wavnc_port_info *portc;
	char rev[3], dev_name[64];
	int my_dev;

	if (waveartist_reset(devc))
		return -ENODEV;

	sprintf(dev_name, "%s (%s", devc->hw.name, devc->chip_name);

	if (waveartist_getrev(devc, rev)) {
		strcat(dev_name, " rev. ");
		strcat(dev_name, rev);
	}
	strcat(dev_name, ")");

	conf_printf2(dev_name, devc->hw.io_base, devc->hw.irq,
		     devc->hw.dma, devc->hw.dma2);

	portc = (wavnc_port_info *)kmalloc(sizeof(wavnc_port_info), GFP_KERNEL);
	if (portc == NULL)
		goto nomem;

	memset(portc, 0, sizeof(wavnc_port_info));

	my_dev = sound_install_audiodrv(AUDIO_DRIVER_VERSION, dev_name,
			&waveartist_audio_driver, sizeof(struct audio_driver),
			devc->audio_flags, AFMT_U8 | AFMT_S16_LE | AFMT_S8,
			devc, devc->hw.dma, devc->hw.dma2);

	if (my_dev < 0)
		goto free;

	audio_devs[my_dev]->portc = portc;

	waveartist_mixer_reset(devc);

	/*
	 * clear any pending interrupt
	 */
	waveartist_iack(devc);

	if (request_irq(devc->hw.irq, waveartist_intr, 0, devc->hw.name, devc) < 0) {
		printk(KERN_ERR "%s: IRQ %d in use\n",
			devc->hw.name, devc->hw.irq);
		goto uninstall;
	}

	if (sound_alloc_dma(devc->hw.dma, devc->hw.name)) {
		printk(KERN_ERR "%s: Can't allocate DMA%d\n",
			devc->hw.name, devc->hw.dma);
		goto uninstall_irq;
	}

	if (devc->hw.dma != devc->hw.dma2 && devc->hw.dma2 != NO_DMA)
		if (sound_alloc_dma(devc->hw.dma2, devc->hw.name)) {
			printk(KERN_ERR "%s: can't allocate DMA%d\n",
				devc->hw.name, devc->hw.dma2);
			goto uninstall_dma;
		}

	waveartist_set_ctlr(&devc->hw, 0, DMA1_IE | DMA0_IE);

	audio_devs[my_dev]->mixer_dev =
		sound_install_mixer(MIXER_DRIVER_VERSION,
				dev_name,
				&waveartist_mixer_operations,
				sizeof(struct mixer_operations),
				devc);

	return my_dev;

uninstall_dma:
	sound_free_dma(devc->hw.dma);

uninstall_irq:
	free_irq(devc->hw.irq, devc);

uninstall:
	sound_unload_audiodev(my_dev);

free:
	kfree(portc);

nomem:
	return -1;
}

static int __init probe_waveartist(struct address_info *hw_config)
{
	wavnc_info *devc = &adev_info[nr_waveartist_devs];

	if (nr_waveartist_devs >= MAX_AUDIO_DEV) {
		printk(KERN_WARNING "waveartist: too many audio devices\n");
		return 0;
	}

	if (check_region(hw_config->io_base, 15))  {
		printk(KERN_WARNING "WaveArtist: I/O port conflict\n");
		return 0;
	}

	if (hw_config->irq > _ISA_IRQ(15) || hw_config->irq < _ISA_IRQ(0)) {
		printk(KERN_WARNING "WaveArtist: Bad IRQ %d\n",
		       hw_config->irq);
		return 0;
	}

	if (hw_config->dma != _ISA_DMA(3)) {
		printk(KERN_WARNING "WaveArtist: Bad DMA %d\n",
		       hw_config->dma);
		return 0;
	}

	hw_config->name = "WaveArtist";
	devc->hw = *hw_config;
	devc->open_mode = 0;
	devc->chip_name = "RWA-010";

	return 1;
}

static void __init attach_waveartist(struct address_info *hw)
{
	wavnc_info *devc = &adev_info[nr_waveartist_devs];

	/*
	 * NOTE! If irq < 0, there is another driver which has allocated the
	 *   IRQ so that this driver doesn't need to allocate/deallocate it.
	 *   The actually used IRQ is ABS(irq).
	 */
	devc->hw = *hw;
	devc->hw.irq = (hw->irq > 0) ? hw->irq : 0;
	devc->open_mode = 0;
	devc->playback_dev = 0;
	devc->record_dev = 0;
	devc->audio_flags = DMA_AUTOMODE;
	devc->levels = levels;

	if (hw->dma != hw->dma2 && hw->dma2 != NO_DMA)
		devc->audio_flags |= DMA_DUPLEX;

	request_region(hw->io_base, 15, devc->hw.name);

	devc->dev_no = waveartist_init(devc);

	if (devc->dev_no < 0)
		release_region(hw->io_base, 15);
	else {
#ifdef CONFIG_ARCH_NETWINDER
		if (machine_is_netwinder()) {
			init_timer(&vnc_timer);
			vnc_timer.function = vnc_slider_tick;
			vnc_timer.expires  = jiffies;
			vnc_timer.data     = nr_waveartist_devs;
			add_timer(&vnc_timer);

			vnc_configure_mixer(devc);
		}
#endif
		nr_waveartist_devs += 1;
	}
}

static void __exit unload_waveartist(struct address_info *hw)
{
	wavnc_info *devc = NULL;
	int i;

	for (i = 0; i < nr_waveartist_devs; i++)
		if (hw->io_base == adev_info[i].hw.io_base) {
			devc = adev_info + i;
			break;
		}

	if (devc != NULL) {
		int mixer;

#ifdef CONFIG_ARCH_NETWINDER
		if (machine_is_netwinder())
			del_timer(&vnc_timer);
#endif

		release_region(devc->hw.io_base, 15);

		waveartist_set_ctlr(&devc->hw, DMA1_IE|DMA0_IE, 0);

		if (devc->hw.irq >= 0)
			free_irq(devc->hw.irq, devc);

		sound_free_dma(devc->hw.dma);

		if (devc->hw.dma != devc->hw.dma2 &&
		    devc->hw.dma2 != NO_DMA)
			sound_free_dma(devc->hw.dma2);

		mixer = audio_devs[devc->dev_no]->mixer_dev;

		if (mixer >= 0)
			sound_unload_mixerdev(mixer);

		if (devc->dev_no >= 0)
			sound_unload_audiodev(devc->dev_no);

		nr_waveartist_devs -= 1;

		for (; i < nr_waveartist_devs; i++)
			adev_info[i] = adev_info[i + 1];
	} else
		printk(KERN_WARNING "waveartist: can't find device "
		       "to unload\n");
}

/*
 * Rebel.com Netwinder specifics...
 */

#define	VNC_TIMER_PERIOD (HZ/4)	//check slider 4 times/sec

#define	MIXER_PRIVATE3_RESET	0x53570000
#define	MIXER_PRIVATE3_READ	0x53570001
#define	MIXER_PRIVATE3_WRITE	0x53570002

#define	VNC_MUTE_INTERNAL_SPKR	0x01	//the sw mute on/off control bit
#define	VNC_MUTE_LINE_OUT	0x10
#define VNC_PHONE_DETECT	0x20
#define VNC_HANDSET_DETECT	0x40
#define VNC_DISABLE_AUTOSWITCH	0x80

extern spinlock_t gpio_lock;

static inline void
vnc_update_spkr_mute(wavnc_info *devc)
{
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);
	cpld_modify(CPLD_UNMUTE, devc->spkr_mute_state ? 0 : CPLD_UNMUTE);
	spin_unlock_irqrestore(&gpio_lock, flags);
}

static void
vnc_mute_lout(wavnc_info *devc, int mute)
{
}

static int
vnc_volume_slider(wavnc_info *devc)
{
	static signed int old_slider_volume;
	unsigned long flags;
	signed int volume = 255;

	*CSR_TIMER1_LOAD = 0x00ffffff;

	save_flags(flags);
	cli();

	outb(0xFF, 0x201);
	*CSR_TIMER1_CNTL = TIMER_CNTL_ENABLE | TIMER_CNTL_DIV1;

	while (volume && (inb(0x201) & 0x01))
		volume--;

	*CSR_TIMER1_CNTL = 0;

	restore_flags(flags);
	
	volume = 0x00ffffff - *CSR_TIMER1_VALUE;


#ifndef REVERSE
	volume = 150 - (volume >> 5);
#else
	volume = (volume >> 6) - 25;
#endif

	if (volume < 0)
		volume = 0;

	if (volume > 100)
		volume = 100;

	/*
	 * slider quite often reads +-8, so debounce this random noise
	 */
	if (abs(volume - old_slider_volume) > 7) {
		old_slider_volume = volume;

		if (debug_flg & DEBUG_MIXER)
			printk(KERN_DEBUG "Slider volume: %d.\n", volume);
	}

	return old_slider_volume;
}

static void
vnc_configure_mixer(wavnc_info *devc)
{
	u_int vals[3];

	if (!devc->no_autoselect) {
		if (devc->handset_detect) {
			devc->recmask = SOUND_MASK_LINE1;
			devc->spkr_mute_state = devc->line_mute_state = 1;
		} else if (devc->telephone_detect) {
			devc->recmask = SOUND_MASK_PHONEIN;
			devc->spkr_mute_state = devc->line_mute_state = 1;
		} else {
			/* unless someone has asked for LINE-IN,
			 * we default to MIC
			 */
			if ((devc->recmask & SOUND_MASK_LINE) == 0)
				devc->recmask = SOUND_MASK_MIC;
			devc->spkr_mute_state = devc->line_mute_state = 0;
		}
		vnc_update_spkr_mute(devc);
		vnc_mute_lout(devc, devc->spkr_mute_state);
	}

	/* Ok.  At this point, we have done the autoswitch logic, or we
	 * have had a command from an ioctl.  We have a valid devc->recmask.
	 * Now we have to connect up the hardware to reflect the recmask.
	 */
	vals[1] = waveartist_cmd1_r(devc, WACMD_GET_LEVEL | 0x800);
	vals[2] = waveartist_cmd1_r(devc, WACMD_GET_LEVEL | 0x900);

	vals[1] &= ~0x3f;

	switch(devc->recmask) {
	case SOUND_MASK_MIC:		/* builtin mic */
		waveartist_cmd1(devc, WACMD_SET_MONO | 0x100);	/* right */
		vals[1] |= 0x28;
		break;

	case SOUND_MASK_LINE1:		/* out handset */
		waveartist_cmd1(devc, WACMD_SET_MONO);		/* left */
		vals[1] |= 0x05;
		break;

	case SOUND_MASK_PHONEIN:	/* our telephone mic */
		waveartist_cmd1(devc, WACMD_SET_MONO);		/* left */
		vals[1] |= 0x04;
		break;

	case SOUND_MASK_LINE:		/* stereo line in */
		vals[1] |= 12;
		break;

	default:
		return;
	}

	vals[0] = WACMD_SET_MIXER;
	waveartist_cmd(devc, 3, vals, 0, NULL);

	waveartist_mixer_update(devc, SOUND_MIXER_IMIX);
}

static int
vnc_slider(wavnc_info *devc)
{
	signed int slider_volume;
	unsigned int temp, old_hs, old_td;

	/*
	 * read the "buttons" state.
	 *  Bit 4 = handset present,
	 *  Bit 5 = offhook
	 */
	temp = inb(0x201) & 0x30;

	old_hs = devc->handset_detect;
	old_td = devc->telephone_detect;

	devc->handset_detect = !(temp & 0x10);
	devc->telephone_detect = !!(temp & 0x20);

	if (!devc->no_autoselect &&
	    (old_hs != devc->handset_detect ||
	     old_td != devc->telephone_detect))
		vnc_configure_mixer(devc);

	slider_volume = vnc_volume_slider(devc);

	/*
	 * If we're using software controlled volume, and
	 * the slider moves by more than 20%, then we
	 * switch back to slider controlled volume.
	 */
	if (abs(devc->slider_vol - slider_volume) > 20)
		devc->use_slider = 1;

	/*
	 * use only left channel
	 */
	temp = levels[SOUND_MIXER_VOLUME] & 0xFF;

	if (slider_volume != temp && devc->use_slider) {
		devc->slider_vol = slider_volume;

		waveartist_mixer_set(devc, SOUND_MIXER_VOLUME,
			slider_volume | slider_volume << 8);

		return 1;
	}

	return 0;
}

static void
vnc_slider_tick(unsigned long data)
{
	int next_timeout;

	if (vnc_slider(adev_info + data))
		next_timeout = 5;	// mixer reported change
	else
		next_timeout = VNC_TIMER_PERIOD;

	mod_timer(&vnc_timer, jiffies + next_timeout);
}

static int
vnc_private_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	wavnc_info *devc = (wavnc_info *)audio_devs[dev]->devc;
	int val;

	switch (cmd) {
	case SOUND_MIXER_PRIVATE1:
	{
		u_int prev_spkr_mute, prev_line_mute, prev_auto_state;
		int val;

		if (get_user(val, (int *)arg))
			return -EFAULT;

		/* check if parameter is logical */
		if (val & ~(VNC_MUTE_INTERNAL_SPKR |
			    VNC_MUTE_LINE_OUT |
			    VNC_DISABLE_AUTOSWITCH))
			return -EINVAL;

		prev_auto_state = devc->no_autoselect;
		prev_spkr_mute  = devc->spkr_mute_state;
		prev_line_mute  = devc->line_mute_state;

		devc->no_autoselect   = (val & VNC_DISABLE_AUTOSWITCH) ? 1 : 0;
		devc->spkr_mute_state = (val & VNC_MUTE_INTERNAL_SPKR) ? 1 : 0;
		devc->line_mute_state = (val & VNC_MUTE_LINE_OUT) ? 1 : 0;

		if (prev_spkr_mute != devc->spkr_mute_state)
			vnc_update_spkr_mute(devc);

		if (prev_line_mute != devc->line_mute_state)
			vnc_mute_lout(devc, devc->line_mute_state);

		if (prev_auto_state != devc->no_autoselect)
			vnc_configure_mixer(devc);

		return 0;
	}

	case SOUND_MIXER_PRIVATE2:
		if (get_user(val, (int *)arg))
			return -EFAULT;

		switch (val) {
#define VNC_SOUND_PAUSE         0x53    //to pause the DSP
#define VNC_SOUND_RESUME        0x57    //to unpause the DSP
		case VNC_SOUND_PAUSE:
			waveartist_cmd1(devc, 0x16);
			break;

		case VNC_SOUND_RESUME:
			waveartist_cmd1(devc, 0x18);
			break;

		default:
			return -EINVAL;
		}
		return 0;

	/* private ioctl to allow bulk access to waveartist */
	case SOUND_MIXER_PRIVATE3:
	{
		unsigned long	flags;
		int		mixer_reg[15], i, val;

		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (copy_from_user(mixer_reg, (void *)val, sizeof(mixer_reg)))
			return -EFAULT;

		switch (mixer_reg[14]) {
		case MIXER_PRIVATE3_RESET:
			waveartist_mixer_reset(devc);
			break;

		case MIXER_PRIVATE3_WRITE:
			waveartist_cmd3(devc, WACMD_SET_MIXER, mixer_reg[0], mixer_reg[4]);
			waveartist_cmd3(devc, WACMD_SET_MIXER, mixer_reg[1], mixer_reg[5]);
			waveartist_cmd3(devc, WACMD_SET_MIXER, mixer_reg[2], mixer_reg[6]);
			waveartist_cmd3(devc, WACMD_SET_MIXER, mixer_reg[3], mixer_reg[7]);
			waveartist_cmd3(devc, WACMD_SET_MIXER, mixer_reg[8], mixer_reg[9]);

			waveartist_cmd3(devc, WACMD_SET_LEVEL, mixer_reg[10], mixer_reg[11]);
			waveartist_cmd3(devc, WACMD_SET_LEVEL, mixer_reg[12], mixer_reg[13]);
			break;

		case MIXER_PRIVATE3_READ:
			spin_lock_irqsave(&waveartist_lock, flags);

			for (i = 0x30; i < 14 << 8; i += 1 << 8)
				waveartist_cmd(devc, 1, &i, 1, mixer_reg + (i >> 8));

			spin_unlock_irqrestore(&waveartist_lock, flags);

			if (copy_to_user((void *)val, mixer_reg, sizeof(mixer_reg)))
				return -EFAULT;
			break;

		default:
			return -EINVAL;
		}
		return 0;
	}

	/* read back the state from PRIVATE1 */
	case SOUND_MIXER_PRIVATE4:
		val = (devc->spkr_mute_state  ? VNC_MUTE_INTERNAL_SPKR : 0) |
		      (devc->line_mute_state  ? VNC_MUTE_LINE_OUT      : 0) |
		      (devc->handset_detect   ? VNC_HANDSET_DETECT     : 0) |
		      (devc->telephone_detect ? VNC_PHONE_DETECT       : 0) |
		      (devc->no_autoselect    ? VNC_DISABLE_AUTOSWITCH : 0);

		return put_user(val, (int *)arg) ? -EFAULT : 0;
	}

	if (((cmd >> 8) & 0xff) == 'M') {
		if (_SIOC_DIR(cmd) & _SIOC_WRITE) {
			/*
			 * special case for master volume: if we
			 * received this call - switch from hw
			 * volume control to a software volume
			 * control, till the hw volume is modified
			 * to signal that user wants to be back in
			 * hardware...
			 */
			if ((cmd & 0xff) == SOUND_MIXER_VOLUME)
				devc->use_slider = 0;
		} else if ((cmd & 0xff) == SOUND_MIXER_STEREODEVS) {
			val = devc->supported_devices &
				~(SOUND_MASK_IMIX |
				  SOUND_MASK_MIC |
				  SOUND_MASK_LINE1 |
				  SOUND_MASK_PHONEIN |
				  SOUND_MASK_PHONEOUT);
			return put_user(val, (int *)arg) ? -EFAULT : 0;
		}
	}

	return -ENOIOCTLCMD;
}

static struct address_info cfg;

static int attached;

static int __initdata io = 0;
static int __initdata irq = 0;
static int __initdata dma = 0;
static int __initdata dma2 = 0;


MODULE_PARM(io, "i");		/* IO base */
MODULE_PARM(irq, "i");		/* IRQ */
MODULE_PARM(dma, "i");		/* DMA */
MODULE_PARM(dma2, "i");		/* DMA2 */

static int __init init_waveartist(void)
{
	if (!io && machine_is_netwinder()) {
		/*
		 * The NetWinder WaveArtist is at a fixed address.
		 * If the user does not supply an address, use the
		 * well-known parameters.
		 */
		io   = 0x250;
		irq  = 12;
		dma  = 3;
		dma2 = 7;
	}

	cfg.io_base = io;
	cfg.irq = irq;
	cfg.dma = dma;
	cfg.dma2 = dma2;

	if (!probe_waveartist(&cfg))
		return -ENODEV;

	attach_waveartist(&cfg);
	attached = 1;

	return 0;
}

static void __exit cleanup_waveartist(void)
{
	if (attached)
		unload_waveartist(&cfg);
}

module_init(init_waveartist);
module_exit(cleanup_waveartist);

#ifndef MODULE
static int __init setup_waveartist(char *str)
{
	/* io, irq, dma, dma2 */
	int ints[5];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io	= ints[1];
	irq	= ints[2];
	dma	= ints[3];
	dma16	= ints[4];

	return 1;
}
__setup("waveartist=", setup_waveartist);
#endif
