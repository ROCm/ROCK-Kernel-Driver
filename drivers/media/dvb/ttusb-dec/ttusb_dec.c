/*
 * TTUSB DEC Driver
 *
 * Copyright (C) 2003 Alex Woods <linux-dvb@giblets.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <asm/semaphore.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>

#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_i2c.h"
#include "dvb_filter.h"
#include "dvb_frontend.h"
#include "dvb_net.h"

static int debug = 0;

#define dprintk	if (debug) printk

#define DRIVER_NAME		"TechnoTrend/Hauppauge DEC USB"

#define COMMAND_PIPE		0x03
#define RESULT_PIPE		0x84
#define STREAM_PIPE		0x88

#define COMMAND_PACKET_SIZE	0x3c
#define ARM_PACKET_SIZE		0x1000

#define ISO_BUF_COUNT		0x04
#define FRAMES_PER_ISO_BUF	0x04
#define ISO_FRAME_SIZE		0x0380

#define	MAX_AV_PES_LENGTH	6144

#define LOF_HI			10600000
#define LOF_LO			9750000

enum ttusb_model {
	TTUSB_DEC2000T,
	TTUSB_DEC3000S
};

struct ttusb_dec {
	enum ttusb_model		model;
	char				*model_name;
	char				*firmware_name;

	/* DVB bits */
	struct dvb_adapter		*adapter;
	struct dmxdev			dmxdev;
	struct dvb_demux		demux;
	struct dmx_frontend		frontend;
	struct dvb_i2c_bus		i2c_bus;
	struct dvb_net			dvb_net;
	struct dvb_frontend_info	*frontend_info;
	int (*frontend_ioctl) (struct dvb_frontend *, unsigned int, void *);

	u16			pid[DMX_PES_OTHER];
	int			hi_band;

	/* USB bits */
	struct usb_device	*udev;
	u8			trans_count;
	unsigned int		command_pipe;
	unsigned int		result_pipe;
	unsigned int		stream_pipe;
	int			interface;
	struct semaphore	usb_sem;

	void			*iso_buffer;
	dma_addr_t		iso_dma_handle;
	struct urb		*iso_urb[ISO_BUF_COUNT];
	int			iso_stream_count;
	struct semaphore	iso_sem;

	u8			av_pes[MAX_AV_PES_LENGTH + 4];
	int			av_pes_state;
	int			av_pes_length;
	int			av_pes_payload_length;

	struct dvb_filter_pes2ts	a_pes2ts;
	struct dvb_filter_pes2ts	v_pes2ts;

	u8			v_pes[16 + MAX_AV_PES_LENGTH];
	int			v_pes_length;
	int			v_pes_postbytes;

	struct list_head	urb_frame_list;
	struct tasklet_struct	urb_tasklet;
	spinlock_t		urb_frame_list_lock;

	int			active; /* Loaded successfully */
};

struct urb_frame {
	u8			data[ISO_FRAME_SIZE];
	int			length;
	struct list_head	urb_frame_list;
};

static struct dvb_frontend_info dec2000t_frontend_info = {
	.name			= "TechnoTrend/Hauppauge DEC2000-t Frontend",
	.type			= FE_OFDM,
	.frequency_min		= 51000000,
	.frequency_max		= 858000000,
	.frequency_stepsize	= 62500,
	.caps =	FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
		FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO |
		FE_CAN_HIERARCHY_AUTO,
};

static struct dvb_frontend_info dec3000s_frontend_info = {
	.name			= "TechnoTrend/Hauppauge DEC3000-s Frontend",
	.type			= FE_QPSK,
	.frequency_min		= 950000,
	.frequency_max		= 2150000,
	.frequency_stepsize	= 125,
	.caps =	FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
		FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO |
		FE_CAN_HIERARCHY_AUTO,
};

static int ttusb_dec_send_command(struct ttusb_dec *dec, const u8 command,
				  int param_length, const u8 params[],
				  int *result_length, u8 cmd_result[])
{
	int result, actual_len, i;
	u8 b[COMMAND_PACKET_SIZE + 4];
	u8 c[COMMAND_PACKET_SIZE + 4];

	dprintk("%s\n", __FUNCTION__);

	if ((result = down_interruptible(&dec->usb_sem))) {
		printk("%s: Failed to down usb semaphore.\n", __FUNCTION__);
		return result;
	}

	b[0] = 0xaa;
	b[1] = ++dec->trans_count;
	b[2] = command;
	b[3] = param_length;

	if (params)
		memcpy(&b[4], params, param_length);

	if (debug) {
		printk("%s: command: ", __FUNCTION__);
		for (i = 0; i < param_length + 4; i++)
			printk("0x%02X ", b[i]);
		printk("\n");
	}

	result = usb_bulk_msg(dec->udev, dec->command_pipe, b, sizeof(b),
			      &actual_len, HZ);

	if (result) {
		printk("%s: command bulk message failed: error %d\n",
		       __FUNCTION__, result);
		up(&dec->usb_sem);
		return result;
	}

	result = usb_bulk_msg(dec->udev, dec->result_pipe, c, sizeof(c),
			      &actual_len, HZ);

	if (result) {
		printk("%s: result bulk message failed: error %d\n",
		       __FUNCTION__, result);
		up(&dec->usb_sem);
		return result;
	} else {
		if (debug) {
			printk("%s: result: ", __FUNCTION__);
			for (i = 0; i < actual_len; i++)
				printk("0x%02X ", c[i]);
			printk("\n");
		}

		if (result_length)
			*result_length = c[3];
		if (cmd_result && c[3] > 0)
			memcpy(cmd_result, &c[4], c[3]);

		up(&dec->usb_sem);

		return 0;
	}
}

static int ttusb_dec_av_pes2ts_cb(void *priv, unsigned char *data)
{
	struct dvb_demux_feed *dvbdmxfeed = (struct dvb_demux_feed *)priv;

	dvbdmxfeed->cb.ts(data, 188, 0, 0, &dvbdmxfeed->feed.ts, DMX_OK);

	return 0;
}

static void ttusb_dec_set_pids(struct ttusb_dec *dec)
{
	u8 b[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
		   0xff, 0xff };

	u16 pcr = htons(dec->pid[DMX_PES_PCR]);
	u16 audio = htons(dec->pid[DMX_PES_AUDIO]);
	u16 video = htons(dec->pid[DMX_PES_VIDEO]);

	dprintk("%s\n", __FUNCTION__);

	memcpy(&b[0], &pcr, 2);
	memcpy(&b[2], &audio, 2);
	memcpy(&b[4], &video, 2);

	ttusb_dec_send_command(dec, 0x50, sizeof(b), b, NULL, NULL);

		dvb_filter_pes2ts_init(&dec->a_pes2ts, dec->pid[DMX_PES_AUDIO],
				       ttusb_dec_av_pes2ts_cb, dec->demux.feed);
		dvb_filter_pes2ts_init(&dec->v_pes2ts, dec->pid[DMX_PES_VIDEO],
				       ttusb_dec_av_pes2ts_cb, dec->demux.feed);
	dec->v_pes_length = 0;
	dec->v_pes_postbytes = 0;
}

static void ttusb_dec_process_av_pes(struct ttusb_dec * dec, u8 * av_pes,
				     int length)
{
	int i;
	u16 csum = 0;
	u8 c;

	if (length < 16) {
		printk("%s: packet too short.\n", __FUNCTION__);
		return;
	}

	for (i = 0; i < length; i += 2) {
		csum ^= le16_to_cpup((u16 *)(av_pes + i));
		c = av_pes[i];
		av_pes[i] = av_pes[i + 1];
		av_pes[i + 1] = c;
	}

	if (csum) {
		printk("%s: checksum failed.\n", __FUNCTION__);
		return;
	}

	if (length > 8 + MAX_AV_PES_LENGTH + 4) {
		printk("%s: packet too long.\n", __FUNCTION__);
		return;
	}

	if (!(av_pes[0] == 'A' && av_pes[1] == 'V')) {
		printk("%s: invalid AV_PES packet.\n", __FUNCTION__);
		return;
	}

	switch (av_pes[2]) {

	case 0x01: {		/* VideoStream */
			int prebytes = av_pes[5] & 0x03;
			int postbytes = (av_pes[5] & 0x0c) >> 2;
			u16 v_pes_payload_length;

			if (dec->v_pes_postbytes > 0 &&
			    dec->v_pes_postbytes == prebytes) {
				memcpy(&dec->v_pes[dec->v_pes_length],
				       &av_pes[12], prebytes);

				dvb_filter_pes2ts(&dec->v_pes2ts, dec->v_pes,
						  dec->v_pes_length + prebytes,
						  1);
			}

			if (av_pes[5] & 0x10) {
				dec->v_pes[7] = 0x80;
				dec->v_pes[8] = 0x05;

				dec->v_pes[9] = 0x21 |
						((av_pes[8] & 0xc0) >> 5);
				dec->v_pes[10] = ((av_pes[8] & 0x3f) << 2) |
						 ((av_pes[9] & 0xc0) >> 6);
				dec->v_pes[11] = 0x01 |
						 ((av_pes[9] & 0x3f) << 2) |
						 ((av_pes[10] & 0x80) >> 6);
				dec->v_pes[12] = ((av_pes[10] & 0x7f) << 1) |
						 ((av_pes[11] & 0xc0) >> 7);
				dec->v_pes[13] = 0x01 |
						 ((av_pes[11] & 0x7f) << 1);

				memcpy(&dec->v_pes[14], &av_pes[12 + prebytes],
				       length - 16 - prebytes);
				dec->v_pes_length = 14 + length - 16 - prebytes;
			} else {
				dec->v_pes[7] = 0x00;
				dec->v_pes[8] = 0x00;

				memcpy(&dec->v_pes[9], &av_pes[8], length - 12);
				dec->v_pes_length = 9 + length - 12;
			}

			dec->v_pes_postbytes = postbytes;

			if (dec->v_pes[9 + dec->v_pes[8]] == 0x00 &&
			    dec->v_pes[10 + dec->v_pes[8]] == 0x00 &&
			    dec->v_pes[11 + dec->v_pes[8]] == 0x01)
				dec->v_pes[6] = 0x84;
			else
				dec->v_pes[6] = 0x80;

			v_pes_payload_length = htons(dec->v_pes_length - 6 +
						     postbytes);
			memcpy(&dec->v_pes[4], &v_pes_payload_length, 2);

			if (postbytes == 0)
				dvb_filter_pes2ts(&dec->v_pes2ts, dec->v_pes,
						  dec->v_pes_length, 1);

			break;
		}

	case 0x02:		/* MainAudioStream */
		dvb_filter_pes2ts(&dec->a_pes2ts, &av_pes[8], length - 12,
				  av_pes[5] & 0x10);
		break;

	default:
		printk("%s: unknown AV_PES type: %02x.\n", __FUNCTION__,
		       av_pes[2]);
		break;

	}
}

static void ttusb_dec_process_urb_frame(struct ttusb_dec * dec, u8 * b,
					int length)
{
	while (length) {
		switch (dec->av_pes_state) {

		case 0:
		case 1:
		case 3:
			if (*b++ == 0xaa) {
				dec->av_pes_state++;
				if (dec->av_pes_state == 4)
					dec->av_pes_length = 0;
			} else {
				dec->av_pes_state = 0;
			}

			length--;
			break;

		case 2:
			if (*b++ == 0x00) {
				dec->av_pes_state++;
			} else {
				dec->av_pes_state = 0;
			}

			length--;
			break;

		case 4:
			dec->av_pes[dec->av_pes_length++] = *b++;

			if (dec->av_pes_length == 8) {
				dec->av_pes_state++;
				dec->av_pes_payload_length = le16_to_cpup(
						(u16 *)(dec->av_pes + 6));
			}

			length--;
			break;

		case 5: {
				int remainder = dec->av_pes_payload_length +
						8 - dec->av_pes_length;

				if (length >= remainder) {
					memcpy(dec->av_pes + dec->av_pes_length,
					       b, remainder);
					dec->av_pes_length += remainder;
					b += remainder;
					length -= remainder;
					dec->av_pes_state++;
				} else {
					memcpy(&dec->av_pes[dec->av_pes_length],
					       b, length);
					dec->av_pes_length += length;
					length = 0;
				}

				break;
			}

		case 6:
			dec->av_pes[dec->av_pes_length++] = *b++;

			if (dec->av_pes_length ==
			    8 + dec->av_pes_payload_length + 4) {
				ttusb_dec_process_av_pes(dec, dec->av_pes,
							 dec->av_pes_length);
				dec->av_pes_state = 0;
			}

			length--;
			break;

		default:
			printk("%s: illegal packet state encountered.\n",
			       __FUNCTION__);
			dec->av_pes_state = 0;

		}

	}
}

static void ttusb_dec_process_urb_frame_list(unsigned long data)
{
	struct ttusb_dec *dec = (struct ttusb_dec *)data;
	struct list_head *item;
	struct urb_frame *frame;
	unsigned long flags;

	while (1) {
		spin_lock_irqsave(&dec->urb_frame_list_lock, flags);
		if ((item = dec->urb_frame_list.next) != &dec->urb_frame_list) {
			frame = list_entry(item, struct urb_frame,
					   urb_frame_list);
			list_del(&frame->urb_frame_list);
		} else {
			spin_unlock_irqrestore(&dec->urb_frame_list_lock,
					       flags);
			return;
		}
		spin_unlock_irqrestore(&dec->urb_frame_list_lock, flags);

		ttusb_dec_process_urb_frame(dec, frame->data, frame->length);
		kfree(frame);
	}
}

static void ttusb_dec_process_urb(struct urb *urb, struct pt_regs *ptregs)
{
	struct ttusb_dec *dec = urb->context;

	if (!urb->status) {
		int i;

		for (i = 0; i < FRAMES_PER_ISO_BUF; i++) {
			struct usb_iso_packet_descriptor *d;

			u8 *b;
			int length;
			struct urb_frame *frame;

			d = &urb->iso_frame_desc[i];
			b = urb->transfer_buffer + d->offset;
			length = d->actual_length;

			if ((frame = kmalloc(sizeof(struct urb_frame),
					     GFP_ATOMIC))) {
				unsigned long flags;

				memcpy(frame->data, b, length);
				frame->length = length;

				spin_lock_irqsave(&dec->urb_frame_list_lock,
						     flags);
				list_add_tail(&frame->urb_frame_list,
					      &dec->urb_frame_list);
				spin_unlock_irqrestore(&dec->urb_frame_list_lock,
						       flags);

				tasklet_schedule(&dec->urb_tasklet);
			}
		}
	} else {
		 /* -ENOENT is expected when unlinking urbs */
		if (urb->status != -ENOENT)
			dprintk("%s: urb error: %d\n", __FUNCTION__,
				urb->status);
	}

	if (dec->iso_stream_count)
		usb_submit_urb(urb, GFP_ATOMIC);
}

static void ttusb_dec_setup_urbs(struct ttusb_dec *dec)
{
	int i, j, buffer_offset = 0;

	dprintk("%s\n", __FUNCTION__);

	for (i = 0; i < ISO_BUF_COUNT; i++) {
		int frame_offset = 0;
		struct urb *urb = dec->iso_urb[i];

		urb->dev = dec->udev;
		urb->context = dec;
		urb->complete = ttusb_dec_process_urb;
		urb->pipe = dec->stream_pipe;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->interval = 1;

		urb->number_of_packets = FRAMES_PER_ISO_BUF;
		urb->transfer_buffer_length = ISO_FRAME_SIZE *
					      FRAMES_PER_ISO_BUF;
		urb->transfer_buffer = dec->iso_buffer + buffer_offset;
		buffer_offset += ISO_FRAME_SIZE * FRAMES_PER_ISO_BUF;

		for (j = 0; j < FRAMES_PER_ISO_BUF; j++) {
			urb->iso_frame_desc[j].offset = frame_offset;
			urb->iso_frame_desc[j].length = ISO_FRAME_SIZE;
			frame_offset += ISO_FRAME_SIZE;
		}
	}
}

static void ttusb_dec_stop_iso_xfer(struct ttusb_dec *dec)
{
	int i;

	dprintk("%s\n", __FUNCTION__);

	if (down_interruptible(&dec->iso_sem))
		return;

	dec->iso_stream_count--;

	if (!dec->iso_stream_count) {
		u8 b0[] = { 0x00 };

		for (i = 0; i < ISO_BUF_COUNT; i++)
			usb_unlink_urb(dec->iso_urb[i]);

		ttusb_dec_send_command(dec, 0x81, sizeof(b0), b0, NULL, NULL);
	}

	up(&dec->iso_sem);
}

/* Setting the interface of the DEC tends to take down the USB communications
 * for a short period, so it's important not to call this function just before
 * trying to talk to it.
 */
static void ttusb_dec_set_streaming_interface(struct ttusb_dec *dec)
{
	if (!dec->interface) {
		usb_set_interface(dec->udev, 0, 8);
		dec->interface = 8;
	}
}

static int ttusb_dec_start_iso_xfer(struct ttusb_dec *dec)
{
	int i, result;

	dprintk("%s\n", __FUNCTION__);

	if (down_interruptible(&dec->iso_sem))
		return -EAGAIN;

	if (!dec->iso_stream_count) {
		u8 b0[] = { 0x05 };

		ttusb_dec_send_command(dec, 0x80, sizeof(b0), b0, NULL, NULL);

		ttusb_dec_setup_urbs(dec);

		for (i = 0; i < ISO_BUF_COUNT; i++) {
			if ((result = usb_submit_urb(dec->iso_urb[i],
						     GFP_ATOMIC))) {
				printk("%s: failed urb submission %d: "
				       "error %d\n", __FUNCTION__, i, result);

				while (i) {
					usb_unlink_urb(dec->iso_urb[i - 1]);
					i--;
				}

				up(&dec->iso_sem);
				return result;
			}
		}

		dec->av_pes_state = 0;
		dec->v_pes_postbytes = 0;
	}

	dec->iso_stream_count++;

	up(&dec->iso_sem);

	return 0;
}

static int ttusb_dec_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct ttusb_dec *dec = dvbdmx->priv;

	dprintk("%s\n", __FUNCTION__);

	if (!dvbdmx->dmx.frontend)
		return -EINVAL;

	dprintk("  pid: 0x%04X\n", dvbdmxfeed->pid);

	switch (dvbdmxfeed->type) {

	case DMX_TYPE_TS:
		dprintk("  type: DMX_TYPE_TS\n");
		break;

	case DMX_TYPE_SEC:
		dprintk("  type: DMX_TYPE_SEC\n");
		break;

	default:
		dprintk("  type: unknown (%d)\n", dvbdmxfeed->type);
		return -EINVAL;

	}

	dprintk("  ts_type:");

	if (dvbdmxfeed->ts_type & TS_DECODER)
		dprintk(" TS_DECODER");

	if (dvbdmxfeed->ts_type & TS_PACKET)
		dprintk(" TS_PACKET");

	if (dvbdmxfeed->ts_type & TS_PAYLOAD_ONLY)
		dprintk(" TS_PAYLOAD_ONLY");

	dprintk("\n");

	switch (dvbdmxfeed->pes_type) {

	case DMX_TS_PES_VIDEO:
		dprintk("  pes_type: DMX_TS_PES_VIDEO\n");
		dec->pid[DMX_PES_PCR] = dvbdmxfeed->pid;
		dec->pid[DMX_PES_VIDEO] = dvbdmxfeed->pid;
		ttusb_dec_set_pids(dec);
		break;

	case DMX_TS_PES_AUDIO:
		dprintk("  pes_type: DMX_TS_PES_AUDIO\n");
		dec->pid[DMX_PES_AUDIO] = dvbdmxfeed->pid;
		ttusb_dec_set_pids(dec);
		break;

	case DMX_TS_PES_TELETEXT:
		dec->pid[DMX_PES_TELETEXT] = dvbdmxfeed->pid;
		dprintk("  pes_type: DMX_TS_PES_TELETEXT\n");
		break;

	case DMX_TS_PES_PCR:
		dprintk("  pes_type: DMX_TS_PES_PCR\n");
		dec->pid[DMX_PES_PCR] = dvbdmxfeed->pid;
		ttusb_dec_set_pids(dec);
		break;

	case DMX_TS_PES_OTHER:
		dprintk("  pes_type: DMX_TS_PES_OTHER\n");
		break;

	default:
		dprintk("  pes_type: unknown (%d)\n", dvbdmxfeed->pes_type);
		return -EINVAL;

	}

	ttusb_dec_start_iso_xfer(dec);

	return 0;
}

static int ttusb_dec_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct ttusb_dec *dec = dvbdmxfeed->demux->priv;

	dprintk("%s\n", __FUNCTION__);

	ttusb_dec_stop_iso_xfer(dec);

	return 0;
}

static void ttusb_dec_free_iso_urbs(struct ttusb_dec *dec)
{
	int i;

	dprintk("%s\n", __FUNCTION__);

	for (i = 0; i < ISO_BUF_COUNT; i++)
		if (dec->iso_urb[i])
			usb_free_urb(dec->iso_urb[i]);

	pci_free_consistent(NULL,
			    ISO_FRAME_SIZE * (FRAMES_PER_ISO_BUF *
					      ISO_BUF_COUNT),
			    dec->iso_buffer, dec->iso_dma_handle);
}

static int ttusb_dec_alloc_iso_urbs(struct ttusb_dec *dec)
{
	int i;

	dprintk("%s\n", __FUNCTION__);

	dec->iso_buffer = pci_alloc_consistent(NULL,
					       ISO_FRAME_SIZE *
					       (FRAMES_PER_ISO_BUF *
						ISO_BUF_COUNT),
				 	       &dec->iso_dma_handle);

	memset(dec->iso_buffer, 0,
	       sizeof(ISO_FRAME_SIZE * (FRAMES_PER_ISO_BUF * ISO_BUF_COUNT)));

	for (i = 0; i < ISO_BUF_COUNT; i++) {
		struct urb *urb;

		if (!(urb = usb_alloc_urb(FRAMES_PER_ISO_BUF, GFP_ATOMIC))) {
			ttusb_dec_free_iso_urbs(dec);
			return -ENOMEM;
		}

		dec->iso_urb[i] = urb;
	}

	ttusb_dec_setup_urbs(dec);

	return 0;
}

static void ttusb_dec_init_tasklet(struct ttusb_dec *dec)
{
	dec->urb_frame_list_lock = SPIN_LOCK_UNLOCKED;
	INIT_LIST_HEAD(&dec->urb_frame_list);
	tasklet_init(&dec->urb_tasklet, ttusb_dec_process_urb_frame_list,
		     (unsigned long)dec);
}

static void ttusb_dec_init_v_pes(struct ttusb_dec *dec)
{
	dprintk("%s\n", __FUNCTION__);

	dec->v_pes[0] = 0x00;
	dec->v_pes[1] = 0x00;
	dec->v_pes[2] = 0x01;
	dec->v_pes[3] = 0xe0;
}

static void ttusb_dec_init_usb(struct ttusb_dec *dec)
{
	dprintk("%s\n", __FUNCTION__);

	sema_init(&dec->usb_sem, 1);
	sema_init(&dec->iso_sem, 1);

	dec->command_pipe = usb_sndbulkpipe(dec->udev, COMMAND_PIPE);
	dec->result_pipe = usb_rcvbulkpipe(dec->udev, RESULT_PIPE);
	dec->stream_pipe = usb_rcvisocpipe(dec->udev, STREAM_PIPE);

	ttusb_dec_alloc_iso_urbs(dec);
}

static int ttusb_dec_boot_dsp(struct ttusb_dec *dec)
{
	int i, j, actual_len, result, size, trans_count;
	u8 b0[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		    0x00 };
	u8 b1[] = { 0x61 };
	u8 b[ARM_PACKET_SIZE];
	u8 *firmware = NULL;
	size_t firmware_size = 0;
	u32 firmware_csum = 0;
	u32 firmware_size_nl;
	u32 firmware_csum_nl;
	const struct firmware *fw_entry = NULL;

	dprintk("%s\n", __FUNCTION__);

	if (request_firmware(&fw_entry, dec->firmware_name, &dec->udev->dev)) {
		printk(KERN_ERR "%s: Firmware (%s) unavailable.\n",
		       __FUNCTION__, dec->firmware_name);
		return 1;
	}

	firmware = fw_entry->data;
	firmware_size = fw_entry->size;

	switch (dec->model) {
		case TTUSB_DEC2000T:
			firmware_csum = 0x1bc86100;
			break;

		case TTUSB_DEC3000S:
			firmware_csum = 0x00000000;
			break;
	}

	firmware_size_nl = htonl(firmware_size);
	memcpy(b0, &firmware_size_nl, 4);
	firmware_csum_nl = htonl(firmware_csum);
	memcpy(&b0[6], &firmware_csum_nl, 4);

	result = ttusb_dec_send_command(dec, 0x41, sizeof(b0), b0, NULL, NULL);

	if (result)
		return result;

	trans_count = 0;
	j = 0;

	for (i = 0; i < firmware_size; i += COMMAND_PACKET_SIZE) {
		size = firmware_size - i;
		if (size > COMMAND_PACKET_SIZE)
			size = COMMAND_PACKET_SIZE;

		b[j + 0] = 0xaa;
		b[j + 1] = trans_count++;
		b[j + 2] = 0xf0;
		b[j + 3] = size;
		memcpy(&b[j + 4], &firmware[i], size);

		j += COMMAND_PACKET_SIZE + 4;

		if (j >= ARM_PACKET_SIZE) {
			result = usb_bulk_msg(dec->udev, dec->command_pipe, b,
					      ARM_PACKET_SIZE, &actual_len,
					      HZ / 10);
			j = 0;
		} else if (size < COMMAND_PACKET_SIZE) {
			result = usb_bulk_msg(dec->udev, dec->command_pipe, b,
					      j - COMMAND_PACKET_SIZE + size,
					      &actual_len, HZ / 10);
		}
	}

	result = ttusb_dec_send_command(dec, 0x43, sizeof(b1), b1, NULL, NULL);

	return result;
}

static int ttusb_dec_init_stb(struct ttusb_dec *dec)
{
	u8 c[COMMAND_PACKET_SIZE];
	int c_length;
	int result;

	dprintk("%s\n", __FUNCTION__);

	result = ttusb_dec_send_command(dec, 0x08, 0, NULL, &c_length, c);

	if (!result) {
		if (c_length != 0x0c || (c_length == 0x0c && c[9] != 0x63))
			return ttusb_dec_boot_dsp(dec);
		else
			return 0;
	}
	else
		return result;
}

static int ttusb_dec_init_dvb(struct ttusb_dec *dec)
{
	int result;

	dprintk("%s\n", __FUNCTION__);

	if ((result = dvb_register_adapter(&dec->adapter, dec->model_name)) < 0) {
		printk("%s: dvb_register_adapter failed: error %d\n",
		       __FUNCTION__, result);

		return result;
	}

	dec->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;

	dec->demux.priv = (void *)dec;
	dec->demux.filternum = 31;
	dec->demux.feednum = 31;
	dec->demux.start_feed = ttusb_dec_start_feed;
	dec->demux.stop_feed = ttusb_dec_stop_feed;
	dec->demux.write_to_decoder = NULL;

	if ((result = dvb_dmx_init(&dec->demux)) < 0) {
		printk("%s: dvb_dmx_init failed: error %d\n", __FUNCTION__,
		       result);

		dvb_unregister_adapter(dec->adapter);

		return result;
	}

	dec->dmxdev.filternum = 32;
	dec->dmxdev.demux = &dec->demux.dmx;
	dec->dmxdev.capabilities = 0;

	if ((result = dvb_dmxdev_init(&dec->dmxdev, dec->adapter)) < 0) {
		printk("%s: dvb_dmxdev_init failed: error %d\n",
		       __FUNCTION__, result);

		dvb_dmx_release(&dec->demux);
		dvb_unregister_adapter(dec->adapter);

		return result;
	}

	dec->frontend.source = DMX_FRONTEND_0;

	if ((result = dec->demux.dmx.add_frontend(&dec->demux.dmx,
						  &dec->frontend)) < 0) {
		printk("%s: dvb_dmx_init failed: error %d\n", __FUNCTION__,
		       result);

		dvb_dmxdev_release(&dec->dmxdev);
		dvb_dmx_release(&dec->demux);
		dvb_unregister_adapter(dec->adapter);

		return result;
	}

	if ((result = dec->demux.dmx.connect_frontend(&dec->demux.dmx,
						      &dec->frontend)) < 0) {
		printk("%s: dvb_dmx_init failed: error %d\n", __FUNCTION__,
		       result);

		dec->demux.dmx.remove_frontend(&dec->demux.dmx, &dec->frontend);
		dvb_dmxdev_release(&dec->dmxdev);
		dvb_dmx_release(&dec->demux);
		dvb_unregister_adapter(dec->adapter);

		return result;
	}

	dvb_net_init(dec->adapter, &dec->dvb_net, &dec->demux.dmx);

	return 0;
}

static void ttusb_dec_exit_dvb(struct ttusb_dec *dec)
{
	dprintk("%s\n", __FUNCTION__);

	dvb_net_release(&dec->dvb_net);
	dec->demux.dmx.close(&dec->demux.dmx);
	dec->demux.dmx.remove_frontend(&dec->demux.dmx, &dec->frontend);
	dvb_dmxdev_release(&dec->dmxdev);
	dvb_dmx_release(&dec->demux);
	dvb_unregister_adapter(dec->adapter);
}

static void ttusb_dec_exit_usb(struct ttusb_dec *dec)
{
	int i;

	dprintk("%s\n", __FUNCTION__);

	dec->iso_stream_count = 0;

	for (i = 0; i < ISO_BUF_COUNT; i++)
		usb_unlink_urb(dec->iso_urb[i]);

	ttusb_dec_free_iso_urbs(dec);
}

static void ttusb_dec_exit_tasklet(struct ttusb_dec *dec)
{
	struct list_head *item;
	struct urb_frame *frame;

	tasklet_kill(&dec->urb_tasklet);

	while ((item = dec->urb_frame_list.next) != &dec->urb_frame_list) {
		frame = list_entry(item, struct urb_frame, urb_frame_list);
		list_del(&frame->urb_frame_list);
		kfree(frame);
	}
}

static int ttusb_dec_2000t_frontend_ioctl(struct dvb_frontend *fe, unsigned int cmd,
				  void *arg)
{
	struct ttusb_dec *dec = fe->data;

	dprintk("%s\n", __FUNCTION__);

	switch (cmd) {

	case FE_GET_INFO:
		dprintk("%s: FE_GET_INFO\n", __FUNCTION__);
		memcpy(arg, dec->frontend_info,
		       sizeof (struct dvb_frontend_info));
		break;

	case FE_READ_STATUS: {
			fe_status_t *status = (fe_status_t *)arg;
			dprintk("%s: FE_READ_STATUS\n", __FUNCTION__);
			*status = FE_HAS_SIGNAL | FE_HAS_VITERBI |
				  FE_HAS_SYNC | FE_HAS_CARRIER | FE_HAS_LOCK;
			break;
		}

	case FE_READ_BER: {
			u32 *ber = (u32 *)arg;
			dprintk("%s: FE_READ_BER\n", __FUNCTION__);
			*ber = 0;
			return -ENOSYS;
			break;
		}

	case FE_READ_SIGNAL_STRENGTH: {
			dprintk("%s: FE_READ_SIGNAL_STRENGTH\n", __FUNCTION__);
			*(s32 *)arg = 0xFF;
			return -ENOSYS;
			break;
		}

	case FE_READ_SNR:
		dprintk("%s: FE_READ_SNR\n", __FUNCTION__);
		*(s32 *)arg = 0;
		return -ENOSYS;
		break;

	case FE_READ_UNCORRECTED_BLOCKS:
		dprintk("%s: FE_READ_UNCORRECTED_BLOCKS\n", __FUNCTION__);
		*(u32 *)arg = 0;
		return -ENOSYS;
		break;

	case FE_SET_FRONTEND: {
			struct dvb_frontend_parameters *p =
				(struct dvb_frontend_parameters *)arg;
			u8 b[] = { 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
				   0x00, 0xff, 0x00, 0x00, 0x00, 0xff };
			u32 freq;

			dprintk("%s: FE_SET_FRONTEND\n", __FUNCTION__);

			dprintk("            frequency->%d\n", p->frequency);
			dprintk("            symbol_rate->%d\n",
				p->u.qam.symbol_rate);
			dprintk("            inversion->%d\n", p->inversion);

			freq = htonl(p->frequency / 1000);
			memcpy(&b[4], &freq, sizeof (u32));
			ttusb_dec_send_command(dec, 0x71, sizeof(b), b, NULL, NULL);

			break;
		}

	case FE_GET_FRONTEND:
		dprintk("%s: FE_GET_FRONTEND\n", __FUNCTION__);
		break;

	case FE_SLEEP:
		dprintk("%s: FE_SLEEP\n", __FUNCTION__);
		return -ENOSYS;
		break;

	case FE_INIT:
		dprintk("%s: FE_INIT\n", __FUNCTION__);
		break;

	case FE_RESET:
		dprintk("%s: FE_RESET\n", __FUNCTION__);
		break;

	default:
		dprintk("%s: unknown IOCTL (0x%X)\n", __FUNCTION__, cmd);
		return -EINVAL;

	}

	return 0;
}

static int ttusb_dec_3000s_frontend_ioctl(struct dvb_frontend *fe, unsigned int cmd,
				  void *arg)
{
	struct ttusb_dec *dec = fe->data;

	dprintk("%s\n", __FUNCTION__);

	switch (cmd) {

	case FE_GET_INFO:
		dprintk("%s: FE_GET_INFO\n", __FUNCTION__);
		memcpy(arg, dec->frontend_info,
		       sizeof (struct dvb_frontend_info));
		break;

	case FE_READ_STATUS: {
			fe_status_t *status = (fe_status_t *)arg;
			dprintk("%s: FE_READ_STATUS\n", __FUNCTION__);
			*status = FE_HAS_SIGNAL | FE_HAS_VITERBI |
				  FE_HAS_SYNC | FE_HAS_CARRIER | FE_HAS_LOCK;
			break;
		}

	case FE_READ_BER: {
			u32 *ber = (u32 *)arg;
			dprintk("%s: FE_READ_BER\n", __FUNCTION__);
			*ber = 0;
			return -ENOSYS;
			break;
		}

	case FE_READ_SIGNAL_STRENGTH: {
			dprintk("%s: FE_READ_SIGNAL_STRENGTH\n", __FUNCTION__);
			*(s32 *)arg = 0xFF;
			return -ENOSYS;
			break;
		}

	case FE_READ_SNR:
		dprintk("%s: FE_READ_SNR\n", __FUNCTION__);
		*(s32 *)arg = 0;
		return -ENOSYS;
		break;

	case FE_READ_UNCORRECTED_BLOCKS:
		dprintk("%s: FE_READ_UNCORRECTED_BLOCKS\n", __FUNCTION__);
		*(u32 *)arg = 0;
		return -ENOSYS;
		break;

	case FE_SET_FRONTEND: {
			struct dvb_frontend_parameters *p =
				(struct dvb_frontend_parameters *)arg;
			u8 b[] = { 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x00 };
			u32 freq;
			u32 sym_rate;
			u32 band;


			dprintk("%s: FE_SET_FRONTEND\n", __FUNCTION__);

			dprintk("            frequency->%d\n", p->frequency);
			dprintk("            symbol_rate->%d\n",
				p->u.qam.symbol_rate);
			dprintk("            inversion->%d\n", p->inversion);

			freq = htonl(p->frequency * 1000 + (dec->hi_band ? LOF_HI : LOF_LO));
			memcpy(&b[4], &freq, sizeof(u32));
			sym_rate = htonl(p->u.qam.symbol_rate);
			memcpy(&b[12], &sym_rate, sizeof(u32));
			band = htonl(dec->hi_band ? LOF_HI : LOF_LO);
			memcpy(&b[24], &band, sizeof(u32));

			ttusb_dec_send_command(dec, 0x71, sizeof(b), b, NULL, NULL);

			break;
		}

	case FE_GET_FRONTEND:
		dprintk("%s: FE_GET_FRONTEND\n", __FUNCTION__);
		break;

	case FE_SLEEP:
		dprintk("%s: FE_SLEEP\n", __FUNCTION__);
		return -ENOSYS;
		break;

	case FE_INIT:
		dprintk("%s: FE_INIT\n", __FUNCTION__);
		break;

	case FE_RESET:
		dprintk("%s: FE_RESET\n", __FUNCTION__);
		break;

	case FE_DISEQC_SEND_MASTER_CMD:
		dprintk("%s: FE_DISEQC_SEND_MASTER_CMD\n", __FUNCTION__);
		break;

	case FE_DISEQC_SEND_BURST:
		dprintk("%s: FE_DISEQC_SEND_BURST\n", __FUNCTION__);
		break;

	case FE_SET_TONE: {
			fe_sec_tone_mode_t tone = (fe_sec_tone_mode_t)arg;
			dprintk("%s: FE_SET_TONE\n", __FUNCTION__);
			dec->hi_band = (SEC_TONE_ON == tone);
			break;
		}

	case FE_SET_VOLTAGE:
		dprintk("%s: FE_SET_VOLTAGE\n", __FUNCTION__);
		break;

	default:
		dprintk("%s: unknown IOCTL (0x%X)\n", __FUNCTION__, cmd);
		return -EINVAL;

	}

	return 0;
}

static void ttusb_dec_init_frontend(struct ttusb_dec *dec)
{
	dec->i2c_bus.adapter = dec->adapter;

	switch (dec->model) {
		case TTUSB_DEC2000T:
			dec->frontend_info = &dec2000t_frontend_info;
			dec->frontend_ioctl = ttusb_dec_2000t_frontend_ioctl;
			break;

		case TTUSB_DEC3000S:
			dec->frontend_info = &dec3000s_frontend_info;
			dec->frontend_ioctl = ttusb_dec_3000s_frontend_ioctl;
			break;
	}

	dvb_register_frontend(dec->frontend_ioctl, &dec->i2c_bus, (void *)dec,
			      dec->frontend_info);
}

static void ttusb_dec_exit_frontend(struct ttusb_dec *dec)
{
	dvb_unregister_frontend(dec->frontend_ioctl, &dec->i2c_bus);
}

static int ttusb_dec_probe(struct usb_interface *intf,
			   const struct usb_device_id *id)
{
	struct usb_device *udev;
	struct ttusb_dec *dec;

	dprintk("%s\n", __FUNCTION__);

	udev = interface_to_usbdev(intf);

	if (!(dec = kmalloc(sizeof(struct ttusb_dec), GFP_KERNEL))) {
		printk("%s: couldn't allocate memory.\n", __FUNCTION__);
		return -ENOMEM;
	}

	usb_set_intfdata(intf, (void *)dec);

	memset(dec, 0, sizeof(struct ttusb_dec));

	switch (id->idProduct) {
		case 0x1006:
			dec->model = TTUSB_DEC3000S;
			dec->model_name = "DEC3000-s";
			dec->firmware_name = "dec3000s.bin";
			break;

		case 0x1008:
			dec->model = TTUSB_DEC2000T;
			dec->model_name = "DEC2000-t";
			dec->firmware_name = "dec2000t.bin";
			break;
	}

	dec->udev = udev;

	ttusb_dec_init_usb(dec);
	if (ttusb_dec_init_stb(dec)) {
		ttusb_dec_exit_usb(dec);
		return 0;
	}
	ttusb_dec_init_dvb(dec);
	ttusb_dec_init_frontend(dec);
	ttusb_dec_init_v_pes(dec);
	ttusb_dec_init_tasklet(dec);

	dec->active = 1;

	ttusb_dec_set_streaming_interface(dec);

	return 0;
}

static void ttusb_dec_disconnect(struct usb_interface *intf)
{
	struct ttusb_dec *dec = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	dprintk("%s\n", __FUNCTION__);

	if (dec->active) {
	ttusb_dec_exit_tasklet(dec);
	ttusb_dec_exit_usb(dec);
		ttusb_dec_exit_frontend(dec);
	ttusb_dec_exit_dvb(dec);
	}

	kfree(dec);
}

static struct usb_device_id ttusb_dec_table[] = {
	{USB_DEVICE(0x0b48, 0x1006)},	/* DEC3000-s */
	/*{USB_DEVICE(0x0b48, 0x1007)},	   Unconfirmed */
	{USB_DEVICE(0x0b48, 0x1008)},	/* DEC2000-t */
	{}
};

static struct usb_driver ttusb_dec_driver = {
      .name		= DRIVER_NAME,
      .probe		= ttusb_dec_probe,
      .disconnect	= ttusb_dec_disconnect,
      .id_table		= ttusb_dec_table,
};

static int __init ttusb_dec_init(void)
{
	int result;

	if ((result = usb_register(&ttusb_dec_driver)) < 0) {
		printk("%s: initialisation failed: error %d.\n", __FUNCTION__,
		       result);
		return result;
	}

	return 0;
}

static void __exit ttusb_dec_exit(void)
{
	usb_deregister(&ttusb_dec_driver);
}

module_init(ttusb_dec_init);
module_exit(ttusb_dec_exit);

MODULE_AUTHOR("Alex Woods <linux-dvb@giblets.org>");
MODULE_DESCRIPTION(DRIVER_NAME);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, ttusb_dec_table);

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug level");
