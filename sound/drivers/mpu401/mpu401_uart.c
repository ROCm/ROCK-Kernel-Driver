/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Routines for control of MPU-401 in UART mode
 *
 *  MPU-401 supports UART mode which is not capable generate transmit
 *  interrupts thus output is done via polling. Also, if irq < 0, then
 *  input is done also via polling. Do not expect good performance.
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
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <sound/core.h>
#include <sound/mpu401.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Routines for control of MPU-401 in UART mode");
MODULE_LICENSE("GPL");

static void snd_mpu401_uart_input_read(mpu401_t * mpu);
static void snd_mpu401_uart_output_write(mpu401_t * mpu);

/*

 */

#define snd_mpu401_input_avail(mpu)	(!(inb(MPU401C(mpu)) & 0x80))
#define snd_mpu401_output_ready(mpu)	(!(inb(MPU401C(mpu)) & 0x40))

#define MPU401_RESET		0xff
#define MPU401_ENTER_UART	0x3f
#define MPU401_ACK		0xfe

static void snd_mpu401_uart_clear_rx(mpu401_t *mpu)
{
	int timeout = 100000;
	for (; timeout > 0 && snd_mpu401_input_avail(mpu); timeout--)
		inb(MPU401D(mpu));
#ifdef CONFIG_SND_DEBUG
	if (timeout <= 0)
		snd_printk("cmd: clear rx timeout (status = 0x%x)\n", inb(MPU401C(mpu)));
#endif
}

static void _snd_mpu401_uart_interrupt(mpu401_t *mpu)
{
	if (test_bit(MPU401_MODE_BIT_INPUT, &mpu->mode))
		snd_mpu401_uart_input_read(mpu);
	else
		snd_mpu401_uart_clear_rx(mpu);
	/* ok. for better Tx performance try do some output when input is done */
	if (test_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode))
		snd_mpu401_uart_output_write(mpu);
}

void snd_mpu401_uart_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	mpu401_t *mpu = snd_magic_cast(mpu401_t, dev_id, return);
	
	if (mpu == NULL)
		return;
	_snd_mpu401_uart_interrupt(mpu);
}

static void snd_mpu401_uart_timer(unsigned long data)
{
	unsigned long flags;
	mpu401_t *mpu = snd_magic_cast(mpu401_t, (void *)data, return);

	spin_lock_irqsave(&mpu->timer_lock, flags);
	/*mpu->mode |= MPU401_MODE_TIMER;*/
	mpu->timer.expires = 1 + jiffies;
	add_timer(&mpu->timer);
	spin_unlock_irqrestore(&mpu->timer_lock, flags);
	if (mpu->rmidi)
		_snd_mpu401_uart_interrupt(mpu);
}

static void snd_mpu401_uart_add_timer (mpu401_t *mpu, int input)
{
	unsigned long flags;

	spin_lock_irqsave (&mpu->timer_lock, flags);
	if (mpu->timer_invoked == 0) {
		mpu->timer.data = (unsigned long)mpu;
		mpu->timer.function = snd_mpu401_uart_timer;
		mpu->timer.expires = 1 + jiffies;
		add_timer(&mpu->timer);
	} 
	mpu->timer_invoked |= input ? MPU401_MODE_INPUT_TIMER : MPU401_MODE_OUTPUT_TIMER;
	spin_unlock_irqrestore (&mpu->timer_lock, flags);
}

static void snd_mpu401_uart_remove_timer (mpu401_t *mpu, int input)
{
	unsigned long flags;

	spin_lock_irqsave (&mpu->timer_lock, flags);
	if (mpu->timer_invoked) {
		mpu->timer_invoked &= input ? ~MPU401_MODE_INPUT_TIMER : ~MPU401_MODE_OUTPUT_TIMER;
		if (! mpu->timer_invoked)
			del_timer(&mpu->timer);
	}
	spin_unlock_irqrestore (&mpu->timer_lock, flags);
}

/*

 */

static void snd_mpu401_uart_cmd(mpu401_t * mpu, unsigned char cmd, int ack)
{
	unsigned long flags;
	int timeout, ok;

	spin_lock_irqsave(&mpu->input_lock, flags);
	if (mpu->hardware != MPU401_HW_TRID4DWAVE) {
		outb(0x00, MPU401D(mpu));
		/*snd_mpu401_uart_clear_rx(mpu);*/
	}
	/* ok. standard MPU-401 initialization */
	if (mpu->hardware != MPU401_HW_SB) {
		for (timeout = 1000; timeout > 0 && !snd_mpu401_output_ready(mpu); timeout--)
			udelay(10);
#ifdef CONFIG_SND_DEBUG
		if (!timeout)
			snd_printk("cmd: tx timeout (status = 0x%x)\n", inb(MPU401C(mpu)));
#endif
	}
	outb(cmd, MPU401C(mpu));
	if (ack) {
		ok = 0;
		timeout = 10000;
		while (!ok && timeout-- > 0) {
			if (snd_mpu401_input_avail(mpu)) {
				if (inb(MPU401D(mpu)) == MPU401_ACK)
					ok = 1;
			}
		}
		if (!ok && inb(MPU401D(mpu)) == MPU401_ACK)
			ok = 1;
	} else {
		ok = 1;
	}
	spin_unlock_irqrestore(&mpu->input_lock, flags);
	if (! ok)
		snd_printk("cmd: 0x%x failed at 0x%lx (status = 0x%x, data = 0x%x)\n", cmd, mpu->port, inb(MPU401C(mpu)), inb(MPU401D(mpu)));
	// snd_printk("cmd: 0x%x at 0x%lx (status = 0x%x, data = 0x%x)\n", cmd, mpu->port, inb(MPU401C(mpu)), inb(MPU401D(mpu)));
}

/*
 * input/output open/close - protected by open_mutex in rawmidi.c
 */
static int snd_mpu401_uart_input_open(snd_rawmidi_substream_t * substream)
{
	mpu401_t *mpu;
	int err;

	mpu = snd_magic_cast(mpu401_t, substream->rmidi->private_data, return -ENXIO);
	if (mpu->open_input && (err = mpu->open_input(mpu)) < 0)
		return err;
	if (! test_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode)) {
		snd_mpu401_uart_cmd(mpu, MPU401_RESET, 1);
		snd_mpu401_uart_cmd(mpu, MPU401_ENTER_UART, 1);
	}
	mpu->substream_input = substream;
	set_bit(MPU401_MODE_BIT_INPUT, &mpu->mode);
	return 0;
}

static int snd_mpu401_uart_output_open(snd_rawmidi_substream_t * substream)
{
	mpu401_t *mpu;
	int err;

	mpu = snd_magic_cast(mpu401_t, substream->rmidi->private_data, return -ENXIO);
	if (mpu->open_output && (err = mpu->open_output(mpu)) < 0)
		return err;
	if (! test_bit(MPU401_MODE_BIT_INPUT, &mpu->mode)) {
		snd_mpu401_uart_cmd(mpu, MPU401_RESET, 1);
		snd_mpu401_uart_cmd(mpu, MPU401_ENTER_UART, 1);
	}
	mpu->substream_output = substream;
	set_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode);
	return 0;
}

static int snd_mpu401_uart_input_close(snd_rawmidi_substream_t * substream)
{
	mpu401_t *mpu;

	mpu = snd_magic_cast(mpu401_t, substream->rmidi->private_data, return -ENXIO);
	clear_bit(MPU401_MODE_BIT_INPUT, &mpu->mode);
	mpu->substream_input = NULL;
	if (! test_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode))
		snd_mpu401_uart_cmd(mpu, MPU401_RESET, 0);
	if (mpu->close_input)
		mpu->close_input(mpu);
	return 0;
}

static int snd_mpu401_uart_output_close(snd_rawmidi_substream_t * substream)
{
	mpu401_t *mpu;

	mpu = snd_magic_cast(mpu401_t, substream->rmidi->private_data, return -ENXIO);
	clear_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode);
	mpu->substream_output = NULL;
	if (! test_bit(MPU401_MODE_BIT_INPUT, &mpu->mode))
		snd_mpu401_uart_cmd(mpu, MPU401_RESET, 0);
	if (mpu->close_output)
		mpu->close_output(mpu);
	return 0;
}

/*
 * trigger input
 */
static void snd_mpu401_uart_input_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	mpu401_t *mpu;
	int max = 64;

	mpu = snd_magic_cast(mpu401_t, substream->rmidi->private_data, return);
	spin_lock_irqsave(&mpu->input_lock, flags);
	if (up) {
		if (! test_and_set_bit(MPU401_MODE_BIT_INPUT_TRIGGER, &mpu->mode)) {
			/* flush FIFO */
			while (max-- > 0)
				inb(MPU401D(mpu));
		}
		if (mpu->irq < 0)
			snd_mpu401_uart_add_timer(mpu, 1);
	} else {
		if (mpu->irq < 0)
			snd_mpu401_uart_remove_timer(mpu, 1);
		clear_bit(MPU401_MODE_BIT_INPUT_TRIGGER, &mpu->mode);
	}
	spin_unlock_irqrestore(&mpu->input_lock, flags);
	if (up)
		snd_mpu401_uart_input_read(mpu);
}

static void snd_mpu401_uart_input_read(mpu401_t * mpu)
{
	int max = 128;
	unsigned char byte;

	/* prevent double enter via event callback */
	if (test_and_set_bit(MPU401_MODE_BIT_RX_LOOP, &mpu->mode))
		return;
	spin_lock(&mpu->input_lock);
	while (max-- > 0) {
		if (snd_mpu401_input_avail(mpu)) {
			byte = inb(MPU401D(mpu));
			if (test_bit(MPU401_MODE_BIT_INPUT_TRIGGER, &mpu->mode)) {
				spin_unlock(&mpu->input_lock);
				snd_rawmidi_receive(mpu->substream_input, &byte, 1);
				spin_lock(&mpu->input_lock);
			}
		} else {
			break; /* input not available */
		}
	}
	spin_unlock(&mpu->input_lock);
	clear_bit(MPU401_MODE_BIT_RX_LOOP, &mpu->mode);
}

/*
 *  Tx FIFO sizes:
 *    CS4237B			- 16 bytes
 *    AudioDrive ES1688         - 12 bytes
 *    S3 SonicVibes             -  8 bytes
 *    SoundBlaster AWE 64       -  2 bytes (ugly hardware)
 */

static void snd_mpu401_uart_output_write(mpu401_t * mpu)
{
	unsigned char byte;
	int max = 256, timeout;

	if (!test_bit(MPU401_MODE_BIT_OUTPUT_TRIGGER, &mpu->mode))
		return;
	/* prevent double enter */
	if (test_and_set_bit(MPU401_MODE_BIT_TX_LOOP, &mpu->mode))
		return;
	do {
		spin_lock(&mpu->output_lock);
		if (snd_rawmidi_transmit_peek(mpu->substream_output, &byte, 1) == 1) {
			for (timeout = 100; timeout > 0; timeout--) {
				if (snd_mpu401_output_ready(mpu)) {
					outb(byte, MPU401D(mpu));
					snd_rawmidi_transmit_ack(mpu->substream_output, 1);
					break;
				}
			}
		} else {
			snd_mpu401_uart_remove_timer (mpu, 0);
			max = 1; /* no other data - leave the tx loop */
		}
		spin_unlock(&mpu->output_lock);
	} while (--max > 0);
	clear_bit(MPU401_MODE_BIT_TX_LOOP, &mpu->mode);
}

static void snd_mpu401_uart_output_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	mpu401_t *mpu;

	mpu = snd_magic_cast(mpu401_t, substream->rmidi->private_data, return);
	spin_lock_irqsave(&mpu->output_lock, flags);
	if (up) {
		set_bit(MPU401_MODE_BIT_OUTPUT_TRIGGER, &mpu->mode);
		snd_mpu401_uart_add_timer(mpu, 0);
	} else {
		snd_mpu401_uart_remove_timer(mpu, 0);
		clear_bit(MPU401_MODE_BIT_OUTPUT_TRIGGER, &mpu->mode);
	}
	spin_unlock_irqrestore(&mpu->output_lock, flags);
	if (up)
		snd_mpu401_uart_output_write(mpu);
}

/*

 */

static snd_rawmidi_ops_t snd_mpu401_uart_output =
{
	.open =		snd_mpu401_uart_output_open,
	.close =	snd_mpu401_uart_output_close,
	.trigger =	snd_mpu401_uart_output_trigger,
};

static snd_rawmidi_ops_t snd_mpu401_uart_input =
{
	.open =		snd_mpu401_uart_input_open,
	.close =	snd_mpu401_uart_input_close,
	.trigger =	snd_mpu401_uart_input_trigger,
};

static void snd_mpu401_uart_free(snd_rawmidi_t *rmidi)
{
	mpu401_t *mpu = snd_magic_cast(mpu401_t, rmidi->private_data, return);
	if (mpu->irq_flags && mpu->irq >= 0)
		free_irq(mpu->irq, (void *) mpu);
	if (mpu->res) {
		release_resource(mpu->res);
		kfree_nocheck(mpu->res);
	}
	snd_magic_kfree(mpu);
}

int snd_mpu401_uart_new(snd_card_t * card, int device,
			unsigned short hardware,
			unsigned long port, int integrated,
			int irq, int irq_flags,
			snd_rawmidi_t ** rrawmidi)
{
	mpu401_t *mpu;
	snd_rawmidi_t *rmidi;
	int err;

	if (rrawmidi)
		*rrawmidi = NULL;
	if ((err = snd_rawmidi_new(card, "MPU-401U", device, 1, 1, &rmidi)) < 0)
		return err;
	mpu = snd_magic_kcalloc(mpu401_t, 0, GFP_KERNEL);
	if (mpu == NULL) {
		snd_device_free(card, rmidi);
		return -ENOMEM;
	}
	rmidi->private_data = mpu;
	rmidi->private_free = snd_mpu401_uart_free;
	spin_lock_init(&mpu->input_lock);
	spin_lock_init(&mpu->output_lock);
	spin_lock_init(&mpu->timer_lock);
	mpu->hardware = hardware;
	if (!integrated) {
		if ((mpu->res = request_region(port, 2, "MPU401 UART")) == NULL) {
			snd_device_free(card, rmidi);
			return -EBUSY;
		}
	}
	mpu->port = port;
	if (irq >= 0 && irq_flags) {
		if (request_irq(irq, snd_mpu401_uart_interrupt, irq_flags, "MPU401 UART", (void *) mpu)) {
			snd_printk("unable to grab IRQ %d\n", irq);
			snd_device_free(card, rmidi);
			return -EBUSY;
		}
		mpu->irq = irq;
		mpu->irq_flags = irq_flags;
	}
	strcpy(rmidi->name, "MPU-401 (UART)");
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_mpu401_uart_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_mpu401_uart_input);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT |
	                     SNDRV_RAWMIDI_INFO_INPUT |
	                     SNDRV_RAWMIDI_INFO_DUPLEX;
	mpu->rmidi = rmidi;
	if (rrawmidi)
		*rrawmidi = rmidi;
	return 0;
}

EXPORT_SYMBOL(snd_mpu401_uart_interrupt);
EXPORT_SYMBOL(snd_mpu401_uart_new);

/*
 *  INIT part
 */

static int __init alsa_mpu401_uart_init(void)
{
	return 0;
}

static void __exit alsa_mpu401_uart_exit(void)
{
}

module_init(alsa_mpu401_uart_init)
module_exit(alsa_mpu401_uart_exit)
